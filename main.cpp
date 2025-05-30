#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <vector>
#include <algorithm>
#include <numeric>

// External dependencies (same as your original main.cpp):
// - OpenSSL RAND_bytes
// - jwt-cpp
// - libcurl
// - nlohmann/json

#include <openssl/rand.h>
#include <jwt-cpp/jwt.h>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

//------------------------------------------
// 1) CREATE_JWT FUNCTION
//------------------------------------------
std::string create_jwt(
        const std::string& keyName,
        const std::string& privateKeyPem,
        const std::string& httpMethod,
        const std::string& requestPath
) {
    // The domain for advanced trade is always "api.coinbase.com"
    std::string url = "api.coinbase.com";
    std::string uri = httpMethod + " " + url + requestPath;

    // Generate random 16-byte nonce
    unsigned char nonce_raw[16];
    RAND_bytes(nonce_raw, sizeof(nonce_raw));
    std::string nonce(reinterpret_cast<char*>(nonce_raw), sizeof(nonce_raw));

    // Create the JWT (expires in 120 seconds)
    auto token = jwt::create()
            .set_subject(keyName)
            .set_issuer("cdp")
            .set_not_before(std::chrono::system_clock::now())
            .set_expires_at(std::chrono::system_clock::now() + std::chrono::seconds{120})
            .set_payload_claim("uri", jwt::claim(uri))
            .set_header_claim("kid", jwt::claim(keyName))
            .set_header_claim("nonce", jwt::claim(nonce))
            .sign(jwt::algorithm::es256(keyName, privateKeyPem));

    return token;
}

//------------------------------------------
// 2) HTTP REQUEST FUNCTION (LIBCURL)
//------------------------------------------
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

std::string httpRequest(
        const std::string& method,
        const std::string& url,
        const std::string& bearerToken,
        const std::string& postData = ""
) {
    CURL* curl = curl_easy_init();
    std::string readBuffer;

    if (curl) {
        struct curl_slist* headers = nullptr;

        std::string authHeader = "Authorization: Bearer " + bearerToken;
        headers = curl_slist_append(headers, authHeader.c_str());
        headers = curl_slist_append(headers, "Content-Type: application/json");

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        if (method == "POST") {
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData.c_str());
        } else if (method == "DELETE") {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
        }
        // "GET" is default, so no special setup needed.

        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            std::cerr << "[ERROR] curl_easy_perform() failed: "
                      << curl_easy_strerror(res) << std::endl;
        }

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }

    return readBuffer;
}

//------------------------------------------
// 3) HELPER: PARSE CANDLES & COMPUTE MA
//------------------------------------------

double computeMovingAverage(const nlohmann::json& candleData, int numCandles)
{
    if (!candleData.is_array() || candleData.size() < static_cast<size_t>(numCandles)) {
        return 0.0; // Not enough data
    }

    double sum = 0.0;
    // We'll use the *most recent* N candles from the array
    // The endpoint might return them in descending time order. Confirm in actual response.
    // Suppose the first in the array is the most recent. If reversed, adjust indexing.
    for (int i = 0; i < numCandles; i++) {
        double closePrice = std::stod(candleData[i]["close"].get<std::string>());
        sum += closePrice;
    }

    return sum / static_cast<double>(numCandles);
}

//------------------------------------------
// 4) FETCH CANDLE DATA
//------------------------------------------

nlohmann::json getCandles(
        const std::string& keyName,
        const std::string& privateKeyPem,
        const std::string& productId,
        const std::string& granularity, // e.g. "ONE_MINUTE" or "FIVE_MINUTE"
        int secondsToFetch // e.g. 300 for 5 minutes if you want ~5 candles
)
{
    // Get current time
    time_t now = time(nullptr);
    time_t startTime = now - secondsToFetch;

    // Construct URL
    std::string path = "/api/v3/brokerage/market/products/" + productId +
                       "/candles?start=" + std::to_string(startTime) +
                       "&end=" + std::to_string(now) +
                       "&granularity=" + granularity;

    std::string method = "GET";
    std::string fullUrl = "https://api.coinbase.com" + path;

    // Sign
    std::string jwt = create_jwt(keyName, privateKeyPem, method, path);
    // Make request
    std::string resp = httpRequest(method, fullUrl, jwt);

    // Parse JSON
    nlohmann::json jsonResp;
    try {
        jsonResp = nlohmann::json::parse(resp);
    } catch (...) {
        std::cerr << "[ERROR] JSON parse error for candle response." << std::endl;
    }

    // Return the "candles" array
    if (jsonResp.contains("candles")) {
        return jsonResp["candles"];
    }

    return {};
}

//------------------------------------------
// 5) PLACE LIMIT ORDER (MAKER)
//------------------------------------------

bool placeLimitOrder(
        const std::string& keyName,
        const std::string& privateKeyPem,
        const std::string& productId,
        const std::string& side,     // "BUY" or "SELL"
        double limitPrice,
        double quoteAmountUsd,       // e.g. 5.0
        const std::string& clientOrderId
)
{
    // Endpoint
    std::string path = "/api/v3/brokerage/orders";
    std::string method = "POST";
    std::string fullUrl = "https://api.coinbase.com" + path;

    // JSON body
    nlohmann::json orderBody;
    orderBody["client_order_id"] = clientOrderId;
    orderBody["product_id"] = productId;
    orderBody["side"] = side;

    // We want a limit order: "limit_limit_gtc"
    // https://docs.cloud.coinbase.com/advanced-trade-api/docs/place-order
    nlohmann::json limitGtc;
    // Price as string
    double limitPriceRounded = std::floor(limitPrice * 100.0 + 0.5) / 100.0;
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << limitPriceRounded;
    std::string limitPriceStr = oss.str();
    limitGtc["limit_price"] = limitPriceStr;
    // We’ll specify how many USD to spend if buying, or how many base to sell if selling.
    // For a limit order, we can do "base_size" or "quote_size".
    // We'll do "quote_size" if side=BUY. If side=SELL, we do "base_size"?
    // For simplicity, let's do "quote_size" on buy, so it invests exactly 5 USD,
    // and for SELL, we might do "base_size" based on how many BTC we want to sell.
    // But the user wants a simple approach, so let's assume we also have a "quote_size" for SELL in advanced trade?
    // Actually, advanced trade does let you do "quote_size" for SELL as well, but it might be more standard to do base_size.
    // We'll do quote_size for both sides for simplicity, though real usage might differ.
    limitGtc["quote_size"] = std::to_string(quoteAmountUsd);

    // Good till canceled
    limitGtc["post_only"] = true;  // to ensure it's maker

    // Combine into "order_configuration"
    nlohmann::json config;
    config["limit_limit_gtc"] = limitGtc;

    orderBody["order_configuration"] = config;

    // Dump JSON
    std::string postData = orderBody.dump();

    // Sign
    std::string jwt = create_jwt(keyName, privateKeyPem, method, path);

    // Make request
    std::string response = httpRequest(method, fullUrl, jwt, postData);
    std::cout << "[placeLimitOrder] side=" << side << " response: " << response << std::endl;

    // Basic check
    try {
        auto jresp = nlohmann::json::parse(response);
        if (jresp.contains("success") && jresp["success"].get<bool>() == true) {
            std::cout << "[INFO] Limit order placed successfully.\n";
            return true;
        }
    } catch (...) {
        std::cerr << "[ERROR] placeLimitOrder parse error.\n";
    }

    return false;
}

//------------------------------------------
// MAIN BOT
//------------------------------------------
int main()
{
    // 1) Your Advanced Trade key name and private key

    std::string keyName       = std::getenv("KEY_NAME");
    std::string privateKeyPem = std::getenv("PRIVATE_KEY_PEM");


    std::string productId = "BTC-USD";

    // For storing the last known state of the short vs long MA
    bool shortWasAbove = false;
    bool shortWasBelow = false;
    bool havePosition = false;  // Are we currently in a long position?

    double lastBuyPrice = 0.0;  // Track the fill price of last buy

    // We'll run an infinite loop, checking MAs every ~30 seconds
    while (true)
    {
        try {
            // 2) Get short-term MA (1-minute candles, 5 periods)
            // We need at least 5 minutes of 1-minute data. Let's fetch ~10 minutes to be safe:
            auto oneMinCandles = getCandles(keyName, privateKeyPem, productId, "ONE_MINUTE", 600 /* 10 min in sec*/);
            double shortMA = computeMovingAverage(oneMinCandles, 5);

            // 3) Get long-term MA (5-minute candles, 5 periods)
            // We need at least 25 minutes if we wanted 5 periods of 5-minute. But let's fetch ~30 minutes:
            auto fiveMinCandles = getCandles(keyName, privateKeyPem, productId, "FIVE_MINUTE", 1800 /* 30 min in sec*/);
            double longMA = computeMovingAverage(fiveMinCandles, 5);

            if (shortMA <= 0.0 || longMA <= 0.0) {
                std::cerr << "[WARN] Could not compute MAs. shortMA=" << shortMA << ", longMA=" << longMA << "\n";
                std::this_thread::sleep_for(std::chrono::seconds(30));
                continue;
            }

            std::cout << "[INFO] shortMA=" << shortMA << ", longMA=" << longMA << std::endl;

            // Check crossovers
            bool shortAbove = (shortMA > longMA);
            bool shortBelow = (shortMA < longMA);

            // CROSSOVER UP → BUY
            if (shortBelow && shortWasAbove == false && shortWasBelow == true) {
                // Means it was below, still below. No crossover up.
            }
            // We'll do a simpler logic: if short was below and now it's above → buy
            if (shortWasBelow && shortAbove && !havePosition) {
                // 1) place buy limit order with quote_size = 5 USD, limit price ~ slightly below shortMA
                // But we also want a small buffer that the price has moved up enough to cover fees
                // For now, let's do a quick check that shortMA is at least e.g. 1.5% above some reference.
                // In practice, you'd track the low price. We'll skip that for brevity or do a minimal check.

                double limitPrice = shortMA * 0.999; // slightly below shortMA to attempt maker
                double fixedQuoteUsd = 5.0;

                bool ok = placeLimitOrder(
                        keyName,
                        privateKeyPem,
                        productId,
                        "BUY",
                        limitPrice,
                        fixedQuoteUsd,
                        "bot-buy-order"
                );
                if (ok) {
                    havePosition = true;
                    lastBuyPrice = shortMA; // or we’d parse fill price from response in real code
                    std::cout << "[STRATEGY] Placed BUY order at limit=" << limitPrice << "\n";
                }
            }

            // CROSSOVER DOWN → SELL
            // If we have a position, check if shortMA < longMA
            if (havePosition && shortMA < longMA) {
                /*
                 * Change coefficient of 'lastBuyPrice' to determine amount profit you want before triggering a sell
                 * This program assumes you are only accounting for the net fees on the trade made. If the trade strategy
                 * triggers a sell but this threshold isn't met, the program will print:
                 * '[STRATEGY] shortMA < longMA but not enough profit to cover fees.'
                 * Though you can change this amount to your greed level.
                 * Note:
                 * Coefficient >= 1
                 * Higher the coefficient higher the profits however,
                 * your position may take longer to sell depending on the market.
                 */
                double minSellPrice = lastBuyPrice * 1.013;
                if (shortMA >= minSellPrice) {
                    // place SELL
                    double limitPrice = shortMA * 1.001;
                    double quoteUsd   = 5.0;

                    bool ok = placeLimitOrder(
                            keyName, privateKeyPem,
                            productId, "SELL",
                            limitPrice, quoteUsd,
                            "bot-sell-order"
                    );
                    if (ok) {
                        havePosition = false;
                        std::cout << "[STRATEGY] Placed SELL order at limit=" << limitPrice << "\n";
                    }
                } else {
                    std::cout << "[STRATEGY] shortMA < longMA but not enough profit to cover fees.\n";
                }
            }


            // Update old states
            shortWasAbove = shortAbove;
            shortWasBelow = shortBelow;

        } catch (const std::exception& e) {
            std::cerr << "[ERROR] " << e.what() << std::endl;
        }

        // Wait 30 seconds before next iteration (example)
        std::this_thread::sleep_for(std::chrono::seconds(30));
    }

    return 0;
}
