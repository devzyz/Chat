#include "../../../GateServer/GateServer/MysqlDao.h"

#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>

int main() {
    using namespace std::chrono_literals;
    try {
        const auto started = std::chrono::steady_clock::now();
        {
            // An empty pool exercises the actual worker and waiting borrowers
            // without opening a database connection or using a test adapter.
            MysqlConnectionPool pool("", "", "", "", 0);
            auto borrower = std::async(std::launch::async, [&pool] { return pool.getConnection(); });
            pool.close();
            pool.close();
            if (borrower.wait_for(1s) != std::future_status::ready || borrower.get()) {
                throw std::runtime_error("closed_pool_borrow_failed");
            }
            if (pool.getConnection()) { throw std::runtime_error("closed_pool_reopened"); }
        }
        { MysqlConnectionPool pool("", "", "", "", 0); }
        if (std::chrono::steady_clock::now() - started > 2s) {
            throw std::runtime_error("pool_shutdown_deadline");
        }
        std::cout << "PASS gate_mysql_pool_lifecycle\n";
        return 0;
    } catch (const std::exception&) {
        std::cerr << "FAIL gate_mysql_pool_lifecycle\n";
        return 1;
    }
}
