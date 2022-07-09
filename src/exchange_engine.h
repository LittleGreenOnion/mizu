#pragma once

#include "order.h"
#include "utils.h"
#include "variadic_table.h"

#include <map>
#include <unordered_map>
#include <shared_mutex>
#include <thread>
#include <condition_variable>
#include <chrono>
#include <iostream>
#include <list>

enum class Response {
  newOrderAck, newOrderReject, cancelAck, cancelReject
};

namespace engineUtils {

struct OrderPriority {
  OrderPriority(const Order& order) :
    price(order.getPrice()),
    isMarketOrder(order.isMarketOrder()),
    side(order.getSide()),
    timestamp(time(0)) {}
  unsigned price;
  bool isMarketOrder;
  bool side;
  size_t timestamp;
};

struct default_order_predicate {
  // Default priority order:
  // 1) Market orders are on the top
  // 2) Higher price
  // 3) Earlier time of arrival
  bool operator()(const OrderPriority &lhs, const OrderPriority &rhs) const {
    if (lhs.side == rhs.side) { /* [[likely]] (since C++20) */
      if (lhs.isMarketOrder != rhs.isMarketOrder)
        return lhs.isMarketOrder;
    }
    else if (lhs.price != rhs.price)
      return lhs.price > rhs.price;
    return lhs.timestamp > rhs.timestamp; 
  }
};

template<typename T = default_order_predicate>
class Orders {
  using order_multimap = std::multimap<OrderPriority, Order, T>;
public:
  explicit Orders(T compare = default_order_predicate{}) : _orders(compare) {}

  typename order_multimap::iterator emplace(Order&& order);

  // Mark order for deletion.
  // @returns true for success, false if order was already finished
  bool markForDeletion(unsigned exchangeId);

  typename order_multimap::iterator last() { 
    if (_orders.empty())
      return _orders.end();
    return std::prev(_orders.end()); 
  }

  // Remove empty orders and orders marked for deletion
  // This method locs _orders
  void eraseMarkedForDeletion();

  typename order_multimap::iterator begin() { return _orders.begin(); }
  typename order_multimap::const_iterator begin() const { return _orders.cbegin(); }

  typename order_multimap::iterator end() { return _orders.end(); }
  typename order_multimap::const_iterator end() const { return _orders.cend(); }

  // Mutex for hash table
  std::shared_timed_mutex _exchangeIdToOrderMutex;

  // Mutex for orders
  mutable std::shared_timed_mutex _ordersMutex;

private:
  // Multimap of orders sorted by priority
  order_multimap _orders;

  // Hash table that helps to find order by given exchange id
  std::unordered_map<unsigned, typename order_multimap::iterator> _exchangeIdToOrder;
};

template<typename T>
typename Orders<T>::order_multimap::iterator Orders<T>::emplace(Order&& order) {
  typename order_multimap::iterator it;
  OrderPriority priority(order);

  {
    std::lock_guard<std::shared_timed_mutex> hashMutex(_exchangeIdToOrderMutex);
    std::lock_guard<std::shared_timed_mutex> orderMutex(_ordersMutex);

    auto exchangeItToOrderIt = _exchangeIdToOrder.find(order.getExchangeId());
    if (exchangeItToOrderIt != _exchangeIdToOrder.end()) {
      // Order with given exchange id already exist (I assumed all exchange ids must be unique)
      return _orders.end();
    }

    it = _orders.emplace(std::piecewise_construct, std::forward_as_tuple(priority), std::forward_as_tuple(std::move(order)));
    _exchangeIdToOrder[order.getExchangeId()] = it;
  }

  return it;
}

template<typename T>
bool Orders<T>::markForDeletion(const unsigned exchangeId) {
  std::shared_lock<std::shared_timed_mutex> hashMutex(_exchangeIdToOrderMutex);
  auto it = _exchangeIdToOrder.find(exchangeId);
  if (it == _exchangeIdToOrder.end())
    return false;

  // Mark order for deletion
  std::lock_guard<std::mutex> orderLock(it->second->second.getMutex());
  it->second->second.markForDeletion();
  return it->second->second.getQuantity();
}

template<typename T>
void Orders<T>::eraseMarkedForDeletion() {
  std::lock_guard<std::shared_timed_mutex> hashMutex(_exchangeIdToOrderMutex);
  std::lock_guard<std::shared_timed_mutex> orderMutex(_ordersMutex);

  std::vector<typename order_multimap::iterator> ordersToDelete;
  std::vector<unsigned> exchangeIdToDelete;

  for (auto it = _orders.begin(); it != _orders.end(); ++it) {
    if (it->second.isMarkedForDeletion() || it->second.getQuantity() == 0) {
      ordersToDelete.push_back(it);
      unsigned exchangeId = it->second.getExchangeId();
      auto exchangeIdIt = _exchangeIdToOrder.find(exchangeId);
      if (exchangeIdIt != _exchangeIdToOrder.end())
        exchangeIdToDelete.push_back(exchangeId);
    }
  }

  for (auto& item : ordersToDelete)
    _orders.erase(item);
  
  for (auto& item : exchangeIdToDelete)
    _exchangeIdToOrder.erase(item);
}

} // namespace engineUtils

// Allows to place, cancel and get infromation about orders
template<typename T = engineUtils::default_order_predicate>
class ExchangeEngine {
public:
  explicit ExchangeEngine(T compare = engineUtils::default_order_predicate{});
  
  ~ExchangeEngine();

  // Place a new order
  // @param an order to be placed
  // @returns an order state after trying to place it
  Response placeOrder(Order&& order);

  // Cancel an outstanding order
  // @param client id for an order to be cancelled
  // @param side
  // @returns an order state after trying to cancell it
  // (I assumed it's enough to know only exchange id and side to cancel orderds)
  Response cancelOrder(const unsigned exchangeId, bool side);

  // Get a state of the specified order
  // @returns an order state for given exchange id
  Response getStateOfOrder(const unsigned exchangeId);

  // Print current market state
  void print() const;

  // @returns last happened transaction
  Transaction getLastTransaction() const;

  // @returns copy of N last transactions
  std::list<Transaction> getLastTransactions(size_t n) const;

private:
  // Update current _marketPrice
  void updateMarketPrice();

  // Remove orders marked for deletion as well as order with empty quantity
  // Also trying to match sell/buy orders
  void update();

  // Add transaction to the history
  void addToHistory(const Transaction& transaction);

private:
  // Response history
  // todo: better to set a limit for max size of history 
  std::list<Transaction> _history;
  mutable std::shared_timed_mutex _historyMutex;

  // Multimap of buy/sell orders sorted by priority of execution
  engineUtils::Orders<T> _buyOrders;
  engineUtils::Orders<T> _sellOrders;

  // Last market price
  std::atomic<unsigned> _marketPrice;

  std::unique_ptr<std::thread> _th;
  std::mutex _cvMutex;
  std::condition_variable _cv;
  bool _terminated = false;
};

template<typename T> ExchangeEngine<T>::ExchangeEngine(T compare /*= engineUtils::default_order_predicate{}*/) :
  _buyOrders(compare), _sellOrders(compare) {
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

template<typename T> ExchangeEngine<T>::~ExchangeEngine() {
  _terminated = true;
  _cv.notify_all();
  if (_th && _th->joinable())
    _th->join();
};

template<typename T>
Response ExchangeEngine<T>::placeOrder(Order&& newOrder) {
  auto& orders = newOrder.getSide() ? _sellOrders : _buyOrders;
  auto it = orders.emplace(std::move(newOrder));
  if (it == orders.end())
    return Response::newOrderReject;

  Order& order = it->second;

  // todo: probably it makes more sense to instantly return newOrderAck and execute the rest of the code in a separate thread?

  updateMarketPrice();

  std::shared_lock<std::shared_timed_mutex> ordersMutex(orders._ordersMutex);


  // For selling order match buying orders and vice versa
  auto& matchingOrders = order.getSide() ? _buyOrders : _sellOrders;
  for (auto& item : matchingOrders) {
    if (order.getQuantity() && item.second.getQuantity())
      addToHistory(order.exchange(item.second, item.second.getClient(), _marketPrice));
  }
  
  return Response::newOrderAck;
}

template<typename T>
Response ExchangeEngine<T>::cancelOrder(const unsigned exchangeId, bool side) {
  if (side)
    return _sellOrders.markForDeletion(exchangeId) ? Response::cancelAck : Response::cancelReject;
  else
    return _buyOrders.markForDeletion(exchangeId) ? Response::cancelAck : Response::cancelReject;
}

template<typename T>
void ExchangeEngine<T>::update() {
  std::cout << "updating" << std::endl;
  _buyOrders.eraseMarkedForDeletion();
  _sellOrders.eraseMarkedForDeletion();

  // If client's balance has been changed, this method will help to execute his orders again with new balance
  // Ideally I would want to iterate over client's orders each time his balance has been changed,
  // but it would make code even more complicated so I decided to make it simpler for this assessment.

  std::shared_lock<std::shared_timed_mutex> buyOrdersMutex(_buyOrders._ordersMutex);
  std::shared_lock<std::shared_timed_mutex> sellOrderMutex(_sellOrders._ordersMutex);

  for (auto& buy : _buyOrders) {
    for (auto& sell : _sellOrders)
      if (buy.second.getQuantity() && sell.second.getQuantity())
        addToHistory(buy.second.exchange(sell.second, sell.second.getClient(), _marketPrice));
  }
}

template<typename T>
void ExchangeEngine<T>::addToHistory(const Transaction& transaction) {
  static const Transaction empty;

  if (transaction == empty)
    return;
  std::lock_guard<std::shared_timed_mutex> lock(_historyMutex);
  _history.emplace_back(transaction);
}

template<typename T>
Transaction ExchangeEngine<T>::getLastTransaction() const {
  std::shared_lock<std::shared_timed_mutex> lock(_historyMutex);
  return _history.back();
}

template<typename T>
std::list<Transaction> ExchangeEngine<T>::getLastTransactions(size_t n) const {
  std::shared_lock<std::shared_timed_mutex> lock(_historyMutex);

  auto end = std::next(_history.begin(), std::min(n, _history.size()));
  return {_history.begin(), end};
}

template<typename T>
void ExchangeEngine<T>::updateMarketPrice() {
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

template<typename T>
void ExchangeEngine<T>::print() const {
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
