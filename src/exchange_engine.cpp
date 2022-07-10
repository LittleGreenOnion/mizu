#include "exchange_engine.h"

using namespace std;

ExchangeEngine::ExchangeEngine() {
  _marketPrice = 0;

  _th = std::make_unique<std::thread>([this]() {
    while (!_terminated) {
      std::unique_lock<std::mutex> lk(_cvMutex);
      if (_cv.wait_for(lk, std::chrono::seconds(5)) == std::cv_status::no_timeout)
        continue;
      update();
    }
  });

};

ExchangeEngine::~ExchangeEngine() {
  _terminated = true;
  _cv.notify_all();
  if (_th && _th->joinable())
    _th->join();
};

Response ExchangeEngine::placeOrder(Order&& newOrder) {
  auto it = newOrder.getSide() ? _sellOrders.emplace(std::move(newOrder)) : _buyOrders.emplace(std::move(newOrder));
  if (newOrder.getSide() ? it == _sellOrders.end() : it == _buyOrders.end())
    return Response::newOrderReject;

  Order& order = it->second;

  // todo: probably it makes more sense to instantly return newOrderAck and execute the rest of the code
  //       in a separate thread using a queue of orders

  updateMarketPrice();

  std::shared_lock<std::shared_timed_mutex> ordersMutex(newOrder.getSide() ? _sellOrders._ordersMutex : _buyOrders._ordersMutex);

  // For selling order match buying orders and vice versa
  unsigned marketPrice = _marketPrice;
  auto price = order.isMarketOrder() ? marketPrice : order.getPrice();

  if (order.getSide()) {
    for (auto& item : _buyOrders) {
      if (order.getQuantity() && item.second.getQuantity()) {
        if (item.second.getPrice() < price)
          break;
        else
          addToHistory(exchange(order, item.second, _marketPrice));
      }
    }
  }
  else {
    for (auto& item : _sellOrders) {
      if (order.getQuantity() && item.second.getQuantity()) {
        if (price < item.second.getPrice())
          break;
        else
          addToHistory(exchange(order, item.second, _marketPrice));
      }
    }
  }
  
  return Response::newOrderAck;
}

Response ExchangeEngine::cancelOrder(const unsigned exchangeId, bool side) {
  if (side)
    return _sellOrders.markForDeletion(exchangeId) ? Response::cancelAck : Response::cancelReject;
  else
    return _buyOrders.markForDeletion(exchangeId) ? Response::cancelAck : Response::cancelReject;
}

void ExchangeEngine::update() {
  std::cout << "updating" << std::endl;
  _buyOrders.eraseMarkedForDeletion();
  _sellOrders.eraseMarkedForDeletion();

  // If client's balance has been changed, this method will help to execute his orders again with new balance
  // Ideally I would want to iterate over client's orders each time his balance has been changed,
  // but it would make code even more complicated so I decided to make it simpler for this assignment.

  std::shared_lock<std::shared_timed_mutex> buyOrdersMutex(_buyOrders._ordersMutex);
  std::shared_lock<std::shared_timed_mutex> sellOrderMutex(_sellOrders._ordersMutex);

  for (auto& buy : _buyOrders) {
    for (auto& sell : _sellOrders) {
      if (buy.second.getQuantity() && sell.second.getQuantity()) {
        if (buy.second.getPrice() < sell.second.getPrice())
          break;
        else
          addToHistory(exchange(buy.second, sell.second, _marketPrice));
      }
    }
  }
}

void ExchangeEngine::addToHistory(const Transaction& transaction) {
  static const Transaction empty;

  if (transaction == empty)
    return;
  std::lock_guard<std::shared_timed_mutex> lock(_historyMutex);
  _history.emplace_back(transaction);
}

Transaction ExchangeEngine::getLastTransaction() const {
  std::shared_lock<std::shared_timed_mutex> lock(_historyMutex);
  return _history.back();
}

std::list<Transaction> ExchangeEngine::getLastTransactions(size_t n) const {
  std::shared_lock<std::shared_timed_mutex> lock(_historyMutex);

  auto end = std::next(_history.begin(), std::min(n, _history.size()));
  return {_history.begin(), end};
}

void ExchangeEngine::updateMarketPrice() {
  // Here I want to find an equilibrium price (where both seller and buyer would be satisfied with a transaction)

  // To make it simpler, I assumed we will always have linear demand and supply equations
  // Ideally I would want to get an approximation for curves of demand and supply so I can have more accurate equations
  // But for this example I went for the easiest solution so I can make it in time

  std::shared_lock<std::shared_timed_mutex> buyOrdersMutex(_buyOrders._ordersMutex);
  std::shared_lock<std::shared_timed_mutex> sellOrderLock(_sellOrders._ordersMutex);

  unsigned x1 = 0, y1 = 0, x2 = 0, y2 = 0;
  for (const auto& item : _buyOrders) {
    // Find the first and last buying order with limit (assuming they would represent dots for a line)
    if (!item.second.isMarketOrder()) {
      x1 = item.second.getQuantity();
      y1 = item.second.getPrice();
      x2 = _buyOrders.last()->second.getQuantity();
      y2 = _buyOrders.last()->second.getPrice();
      break;
    }
  }

  unsigned x3 = 0, y3 = 0, x4 = 0, y4 = 0;
  for (const auto& item : _sellOrders) {
    // Find the first and last selling order with limit (assuming they would represent dots for a line)
    if (!item.second.isMarketOrder()) {
      x3 = item.second.getQuantity();
      y3 = item.second.getPrice();
      x4 = _buyOrders.last()->second.getQuantity();
      y4 = _buyOrders.last()->second.getPrice();
      break;
    }
  }

  auto equilibrium = getLineIntersection(x1, y1, x2, y2, x3, y3, x4, y4);
  if (equilibrium.second != std::numeric_limits<double>::max())
    _marketPrice = static_cast<unsigned>(equilibrium.second);
}

void ExchangeEngine::print() const {
  VariadicTable<unsigned, unsigned, unsigned, unsigned, std::string, std::string> vt({"client id", "exchange id", "price", "quantity", "is market price", "side"});

  unsigned marketPrice = _marketPrice;
  std::shared_lock<std::shared_timed_mutex> buyOrdersMutex(_buyOrders._ordersMutex);
  for (const auto& item : _buyOrders)
    vt.addRow(item.second.getClientId(), item.second.getExchangeId(), item.second.isMarketOrder() ? marketPrice : item.second.getPrice(),
              item.second.getQuantity(), item.second.isMarketOrder() ? "yes" : "no", "buy");

  std::shared_lock<std::shared_timed_mutex> sellOrderLock(_sellOrders._ordersMutex);
  for (const auto& item : _sellOrders)
    vt.addRow(item.second.getClientId(), item.second.getExchangeId(), item.second.isMarketOrder() ? marketPrice : item.second.getPrice(),
              item.second.getQuantity(), item.second.isMarketOrder() ? "yes" : "no", "sell");

  vt.print(std::cout);
}