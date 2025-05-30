#include "pch.h"
#include "ccapi_cpp/ccapi_session.h"
#include <thread>
#include <iostream>

// Initialize the CCAPI logger.
namespace ccapi {
    Logger* Logger::logger = nullptr;
}

void runCCAPISession() {
    // 1) Create session options and configs.
    ccapi::SessionOptions sessionOptions;
    ccapi::SessionConfigs sessionConfigs;

    // 2) Define an event handler.
    class MyEventHandler : public ccapi::EventHandler {
    public:
        bool processEvent(const ccapi::Event& event, ccapi::Session* session) override {
            std::cout << "Received event:\n"
                      << event.toStringPretty(2, 2) << std::endl;
            return true;
        }
    } eventHandler;

    // 3) Create a session.
    ccapi::Session session(sessionOptions, sessionConfigs, &eventHandler);

    // 4) Construct a "get recent trades" request for Coinbase BTC-USD.
    ccapi::Request request(ccapi::Request::Operation::GET_RECENT_TRADES, "coinbase", "BTC-USD");
    request.appendParam({{"LIMIT", "1"}});
    session.sendRequest(request);

    // 5) Run the session briefly to receive data.
    std::this_thread::sleep_for(std::chrono::seconds(5));
    session.stop();
}
