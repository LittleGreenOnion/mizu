#include "order.h"
#include <iostream>

using namespace std;

bool operator==(const Transaction& lhs, const Transaction& rhs) {
  return lhs._exchangeIdBuyer == rhs._exchangeIdBuyer && lhs._exchangeIdSeller == rhs._exchangeIdSeller &&
         lhs._sold == rhs._sold && lhs._price == rhs._price;
}

Transaction Order::exchange(Order& target, shared_ptr<Trader> targetTrader, unsigned marketPrice) {
  static const Transaction empty;
  if (getSide() == target.getSide()) /*[[unlikely]] (since C++20)*/
    return empty;

  Order& sell = getSide() ? *this : target;
  Order& buy  = getSide() ? target : *this;

  if (sell.getClientId() == buy.getClientId())
    return empty;

  if (sell.getQuantity() == 0 || buy.getQuantity() == 0)
    return empty;

  // Try to match limits. For market orders market price is used as the limit
  unsigned buyPrice = buy.isMarketOrder() ? marketPrice : buy.getPrice();
  unsigned sellPrice = sell.isMarketOrder() ? marketPrice : sell.getPrice();

  if (buyPrice < sellPrice)
    return empty;

  auto buyer = getSide() ? targetTrader : _client;

  lock_guard<mutex> buyMutex(buy.getMutex());
  lock_guard<mutex> sellMutex(sell.getMutex());

  if (buy.isMarkedForDeletion() || sell.isMarkedForDeletion())
    return empty;

  // Find an equilibrium price
  const unsigned price = (buyPrice + sellPrice) / 2;

  do {
    // Find how many orders buyer can purchase
    const unsigned maxQuantity = std::min(sell.getQuantity(), buy.getQuantity());
    const unsigned quantity = std::min(buyer->getBalance() / price, maxQuantity);
    if (quantity == 0)
      break;

    if (!buyer->decreaseBalance(quantity * price))
      continue; // Failed to purchase, try again

    auto seller = getSide() ? _client : targetTrader;
    seller->increaseBalance(quantity * price);

    buy.decreaseQuantity(quantity);
    sell.decreaseQuantity(quantity);


   cout << "Exchange: id = " << buy.getExchangeId() << ", price = " << buyPrice << ", quantity = " << buy.getQuantity() << "   and   " <<
           "Exchange: id = " << sell.getExchangeId() << ", price = " << sellPrice << ", quantity = " << sell.getQuantity() <<
           " | sold quantity " << quantity << ", price " << price << ", marketPrice: " << marketPrice << endl;

    return Transaction(sell.getExchangeId(), buy.getExchangeId(), quantity, price);
  } while (true);

  return empty;
}
