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

struct default_buy_order_predicate {
  // Default priority order:
  // 1) Market orders are on the top
  // 2) Higher price
  // 3) Earlier time of arrival
  bool operator()(const OrderPriority &lhs, const OrderPriority &rhs) const {
    if (lhs.side == rhs.side && lhs.isMarketOrder != rhs.isMarketOrder)
      return lhs.isMarketOrder;
    else if (lhs.price != rhs.price)
      return lhs.price > rhs.price;
    return lhs.timestamp > rhs.timestamp; 
  }
};

struct default_sell_order_predicate {
  // Default priority order:
  // 1) Market orders are on the top
  // 2) Lower price
  // 3) Earlier time of arrival
  bool operator()(const OrderPriority &lhs, const OrderPriority &rhs) const {
    if (lhs.side == rhs.side && lhs.isMarketOrder != rhs.isMarketOrder)
      return lhs.isMarketOrder;
    else if (lhs.price != rhs.price)
      return lhs.price < rhs.price;
    return lhs.timestamp > rhs.timestamp; 
  }
};

template<typename T>
class Orders {
  using order_multimap = std::multimap<OrderPriority, Order, T>;
public:
  Orders(T compare = T{}) : _orders(compare) {}

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
class ExchangeEngine {
public:
  explicit ExchangeEngine();
  
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
  engineUtils::Orders<engineUtils::default_buy_order_predicate> _buyOrders;
  engineUtils::Orders<engineUtils::default_sell_order_predicate> _sellOrders;

  // Last market price
  std::atomic<unsigned> _marketPrice;

  std::unique_ptr<std::thread> _th;
  std::mutex _cvMutex;
  std::condition_variable _cv;
  bool _terminated = false;
};
