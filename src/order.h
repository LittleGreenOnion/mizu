#pragma once

#include "trader.h"

#include <limits>
#include <mutex>
#include <memory>

// Struct that contains information about completed transaction
struct Transaction {
  Transaction(unsigned exchangeIdSeller = 0, unsigned exchangeIdBuyer = 0, unsigned sold = 0, unsigned price = 0) :
    _exchangeIdSeller(exchangeIdSeller), _exchangeIdBuyer(exchangeIdBuyer), _sold(sold), _price(price) {}
  unsigned _exchangeIdSeller;
  unsigned _exchangeIdBuyer;

  unsigned _sold;
  unsigned _price;
};

bool operator==(const Transaction& lhs, const Transaction& rhs);

struct Order {
  Order(std::shared_ptr<Trader> client, unsigned exchangeId, unsigned price, unsigned quantity, bool side, bool isMarketOrder) noexcept :
    _client(client),
    _exchangeId(exchangeId),
    _price(isMarketOrder ? (side ? 0 : std::numeric_limits<unsigned>::max()) : price),
    _quantity(quantity),
    _side(side),
    _isMarketOrder(isMarketOrder) {}
  
  Order(Order&& order) :
    _client(order._client),
    _exchangeId(order._exchangeId),
    _price(order._price),
    _quantity(order._quantity),
    _side(order._side),
    _isMarketOrder(order._isMarketOrder) {}
  
  // @returns shared_ptr for Trader
  std::shared_ptr<Trader> getClient() const {
    return _client;
  }

  // @returns client id
  unsigned getClientId() const {
    return _client ? _client->getId() : 0;
  }

  // @returns exchange id
  unsigned getExchangeId() const {
    return _exchangeId;
  }

  // Return Min/Max price for selling/buying
  // @returns current min/max price for selling/buying for limit orders,
  //          0 for selling market orders
  //          max unsigned value for buying market orders
  unsigned getPrice() const {
    return _price;
  }

  // @returns quantity to be sold/bought
  unsigned getQuantity() const {
    return _quantity;
  }

  // @returns true for selling orders, false for buying orders
  bool getSide() const {
    return _side;
  }

  // @returns true for market orders
  bool isMarketOrder() const {
    return _isMarketOrder;
  }

  // Decrease order quantity
  // @param quantity to be reduced
  void decreaseQuantity(unsigned quantity) {
    if (_quantity < quantity)
      throw std::invalid_argument("Can't decrease quantity");
    _quantity -= quantity;
  }

  // @returns Order's mutex
  std::mutex& getMutex() {
    return _mutex;
  }

  void markForDeletion() {
    _toDelete = true;
  }

  bool isMarkedForDeletion() {
    return _toDelete;
  }

private:
  std::shared_ptr<Trader> _client;
  const unsigned _exchangeId;
  const unsigned _price;     // Min/Max price for selling/buying for limit orders
  unsigned _quantity;        // Current quantity to be sold or bought
  const bool _side;          // Sell == true, buy == false
  const bool _isMarketOrder; // Market orders are transactions meant to execute as quickly as possible at the current market price.;

  std::mutex _mutex;
  bool _toDelete = false;
};

// Try to make a transaction between orders
// @param order to sell/buy
// @param order to sell/buy
// @param market price
// @returns a transaction result. empty transaction if the transaction is unsuccessful.
Transaction exchange(Order& leftOrder, Order& rightOrder, unsigned marketPrice);
