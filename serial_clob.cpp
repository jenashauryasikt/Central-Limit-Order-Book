#include <iostream>
#include <fstream>
#include <sstream>
#include <map>
#include <set>
#include <vector>
#include <chrono>
#include <thread>
#include <iomanip>
#include <algorithm>
#include <queue>
#include <ncurses.h>    // for window/screen manipulation
#include <ctime>

// for maintaining output display
int counter = 46;
int width = 0;


struct Order {
    std::string orderCommand;
    std::string orderID;
    std::string timestamp;
    std::string symbol;
    char type;  // 'B' - buy, 'S' - sell
    int size;
    double price;
    bool isMarketOrder;

    bool operator<(const Order& other) const {
        return timestamp < other.timestamp;
    }
};

struct OrderCompare {
    bool operator()(const Order& a, const Order& b) const {
        if (a.price != b.price) return a.type == 'B' ? a.price > b.price : a.price < b.price; // for buy and sell resp.
        return a.timestamp < b.timestamp;
    }
};

class CLOB {
private:
    // mapped multisets for fastest insertion, search, deletion
    std::map<std::string, std::multiset<Order, OrderCompare>> buyOrders;
    std::map<std::string, std::multiset<Order, OrderCompare>> sellOrders;

public:
    void addOrder(const Order& order) {     // for limit orders
        if (order.isMarketOrder) {          // send market orders to diff function
            processMarketOrder(order);
        } else {
            if (order.type == 'B') {
                buyOrders[order.symbol].insert(order);
            } else {
                sellOrders[order.symbol].insert(order);
            }
            mvprintw(counter++, width, "%s added to %s", order.orderID.c_str(), order.symbol.c_str()); //mvprintw for text placing on windows
            refresh();
            if (counter == LINES){  // new column if run out of window size
                counter = 46;
                width += 22;
            }
        }
    }

    void cancelOrder(const Order& order) {
        auto& orders = (order.type == 'B') ? buyOrders[order.symbol] : sellOrders[order.symbol]; // determine orders multiset
        auto it = std::find_if(orders.begin(), orders.end(),
            [&](const Order& o) { return o.orderID == order.orderID; });    // give iterator matching order to be cancelled
        if (it != orders.end()) {
            orders.erase(it);
            mvprintw(counter++, width, "%s cancelled", order.orderID.c_str());
            refresh();
            if (counter == LINES){
                counter = 46;
                width += 22;
            }
        }
    }

    // market orders
    void processMarketOrder(Order order) {
        auto& counterOrders = (order.type == 'B') ? sellOrders[order.symbol] : buyOrders[order.symbol]; // market will buy from ask and sell to bid
        auto it = counterOrders.begin();
        while (order.size > 0 && it != counterOrders.end()) {
            if (order.size >= it->size) {
                order.size -= it->size;
                mvprintw(counter++, width, "%s hit", it->orderID.c_str());
                refresh();
                if (counter == LINES){
                    counter = 46;
                    width += 22;
                }
                it = counterOrders.erase(it);   // satisfied limit order
            } else {
                Order updatedOrder = *it;       // update partially satisfied limit order
                updatedOrder.size -= order.size;
                counterOrders.erase(it);            
                counterOrders.insert(updatedOrder);
                order.size = 0;
            }
        }
        if (order.size == 0) {      // satisfied market order
            mvprintw(counter++, width, "%s completed", order.orderID.c_str());
        } else {
            mvprintw(counter++, width, "%s partially filled, remaining size: %d", order.orderID.c_str(), order.size);
        }
        refresh();
        if (counter == LINES){
                counter = 46;
                width += 22;
            }
    }

    std::string getCLOBString(const std::string& symbol) { // clob output for a stock symbol
        std::ostringstream oss;
        oss << "CLOB for " << symbol << std::endl;
        oss << std::setw(10) << "Level" << std::setw(10) << "OrderID" << std::setw(10) << "Time"
            << std::setw(10) << "BidSize" << std::setw(10) << "BidPrice" << std::setw(10) << "AskPrice"
            << std::setw(10) << "AskSize" << std::setw(10) << "Time" << std::setw(10) << "OrderID"
            << std::setw(10) << "Level" << std::endl;

        auto buyIt = buyOrders[symbol].begin();
        auto sellIt = sellOrders[symbol].begin();
        int level = 1;

        while (buyIt != buyOrders[symbol].end() || sellIt != sellOrders[symbol].end()) {
            oss << std::setw(10) << level;
            
            if (buyIt != buyOrders[symbol].end()) {
                oss << std::setw(10) << buyIt->orderID << std::setw(10) << buyIt->timestamp
                    << std::setw(10) << buyIt->size << std::setw(10) << std::fixed << std::setprecision(2) << buyIt->price;
                ++buyIt;
            } else {
                oss << std::setw(40) << "";
            }

            if (sellIt != sellOrders[symbol].end()) {
                oss << std::setw(10) << std::fixed << std::setprecision(2) << sellIt->price << std::setw(10) << sellIt->size
                    << std::setw(10) << sellIt->timestamp << std::setw(10) << sellIt->orderID;
                ++sellIt;
            } else {
                oss << std::setw(40) << "";
            }

            oss << std::setw(10) << level << std::endl;
            ++level;
        }
        return oss.str();
    }
};

Order parseCSVLine(const std::string& line) { // read from csv
    std::istringstream iss(line);
    std::vector<std::string> tokens;
    std::string token;
    while (std::getline(iss, token, ',')) {
        tokens.push_back(token);
    }

    Order order;
    order.orderCommand = tokens[0];
    order.orderID = tokens[1];
    order.timestamp = tokens[2];
    order.symbol = tokens[3];
    order.type = (tokens[4] == "BUY") ? 'B' : 'S';
    order.size = std::stoi(tokens[5]);
    order.isMarketOrder = tokens[6].empty();    // market order if no bid/ask price
    order.price = order.isMarketOrder ? 0.0 : std::stod(tokens[6]);

    return order;
}

std::chrono::system_clock::time_point parseTimestamp(const std::string& timestamp) {
    std::istringstream iss(timestamp);
    int hour, minute, second;
    char colon;
    iss >> hour >> colon >> minute >> colon >> second;
    
    auto now = std::chrono::system_clock::now();
    time_t tnow = std::chrono::system_clock::to_time_t(now);
    tm *date = std::localtime(&tnow);
    date->tm_hour = hour;
    date->tm_min = minute;
    date->tm_sec = second;
    
    return std::chrono::system_clock::from_time_t(std::mktime(date));
}

struct OrderComparator { // FIFO
    bool operator()(const std::pair<std::chrono::system_clock::time_point, Order>& a,
                    const std::pair<std::chrono::system_clock::time_point, Order>& b) const {
        return a.first > b.first;
    }
};

int main() {
    CLOB clob;
    std::vector<std::string> symbols = {"AAPL", "INTC", "NVDA"};
    std::vector<std::string> userFiles = {"user_1.csv", "user_2.csv", "user_3.csv"};
    std::string marketFile = "market.csv";

    // init window manipulaion
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);

    // windows for each stock
    WINDOW* windows[3];
    for (int i = 0; i < 3; ++i) {
        windows[i] = newwin(15, COLS, i * 15, 0); // 15 lines for each CLOB
        scrollok(windows[i], TRUE);
    }

    std::priority_queue<std::pair<std::chrono::system_clock::time_point, Order>,
                        std::vector<std::pair<std::chrono::system_clock::time_point, Order>>,
                        OrderComparator> orderQueue;

    // queue all orders
    for (const auto& file : userFiles) {
        std::ifstream ifs(file);
        std::string line;
        std::getline(ifs, line);  // skip headers
        while (std::getline(ifs, line)) {
            Order order = parseCSVLine(line);
            auto timestamp = parseTimestamp(order.timestamp);
            orderQueue.push(std::make_pair(timestamp, order));
        }
    }

    std::ifstream marketIfs(marketFile);
    std::string line;
    std::getline(marketIfs, line);  // skip headers
    while (std::getline(marketIfs, line)) {
        Order order = parseCSVLine(line);
        auto timestamp = parseTimestamp(order.timestamp);
        orderQueue.push(std::make_pair(timestamp, order));
    }

    auto startTime = std::chrono::system_clock::now();
    auto endTime = startTime + std::chrono::minutes(60); // time to view sim results

    while (!orderQueue.empty() && std::chrono::system_clock::now() < endTime) {
        auto top = orderQueue.top();
        auto timestamp = top.first;
        auto order = top.second;
        orderQueue.pop();

        std::this_thread::sleep_until(startTime + (timestamp - parseTimestamp("12:00:00"))); // input orders in real time 

        if (order.isMarketOrder) {
            clob.processMarketOrder(order);
        } else if (order.orderCommand != "Cancel") {
            clob.addOrder(order);
        } else {
            clob.cancelOrder(order);
        }

        // update CLOB displays
        for (int i = 0; i < 3; ++i) {
            wclear(windows[i]);
            mvwprintw(windows[i], 0, 0, clob.getCLOBString(symbols[i]).c_str());
            wrefresh(windows[i]);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));  // shortest delay detectable by human eye
    }

    // clean up windows
    std::this_thread::sleep_for(std::chrono::minutes(60)); // tmie to view outputs
    getch();
    endwin();

    return 0;
}