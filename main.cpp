#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <vector>
#include <algorithm>
#include <numeric>

// External dependencies:
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
        const std::string& keyName, // Key ID
        const std::string& privateKeyPem, // Private Key
        const std::string& httpMethod, // "Delete" (not used), "Get", "Post"
        const std::string& requestPath // Relative Path to Endpoint
) {
    // Creating URI
    // The domain for advanced trade is always "api.coinbase.com"
    std::string url = "api.coinbase.com";
    std::string uri = httpMethod + " " + url + requestPath;

    // Generate Random 16-byte Nonce
    // Ensuring Each JWT is Unique
    unsigned char nonce_raw[16];
    RAND_bytes(nonce_raw, sizeof(nonce_raw));
    std::string nonce(reinterpret_cast<char*>(nonce_raw), sizeof(nonce_raw));

    // Create the JWT (expires in 120 seconds)
    // Signing with ES256 Elliptical Curve
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
    ((std::string*)userp)->append((char*)contents, size * nmemb); // Appends Received Response JSON in String format to readBuffer (httpRequest) or userp
    return size * nmemb; // Returns size Total Number of Bytes or the actual length of data received
}

std::string httpRequest(
        const std::string& method, // "Delete" (Not Used), "Get", "Post"
        const std::string& url, // Full Url
        const std::string& bearerToken, // Signed JWT
        const std::string& postData = "" // JSON Body for Post
) {
    // Creating a new Curl Session
    // Initializing our return output (String type, JSON format)
    CURL* curl = curl_easy_init();
    std::string readBuffer;

    if (curl) {
        struct curl_slist* headers = nullptr;

        std::string authHeader = "Authorization: Bearer " + bearerToken;
        headers = curl_slist_append(headers, authHeader.c_str()); // JWT as Bearer Token
        headers = curl_slist_append(headers, "Content-Type: application/json"); // Specify JSON to Curl

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str()); // Add full Url
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers); // Add the JWT

        if (method == "POST") {
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData.c_str()); // Post Specified Data
        } else if (method == "DELETE") {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE"); // Not used in this
        }
        // "GET" is default, so no special setup needed.

        // Call WriteCallback When Return Data Received
        // Pass readBuffer as "userp" and Gather Return Data (As string type, in JSON format)
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

        // Execute
        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            std::cerr << "[ERROR] curl_easy_perform() failed: "
                      << curl_easy_strerror(res) << std::endl;
        }

        // Wrap up
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }

    // All Returned Data, String type, JSON format
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

    // Initialize our total
    double sum = 0.0;

    // Use the most recent N candles from the array
    // Suppose the first in the array is the most recent
    // Loop through candles and get most recent Close Data from the array
    for (int i = 0; i < numCandles; i++) {
        double closePrice = std::stod(candleData[i]["close"].get<std::string>());
        sum += closePrice;
    }

    // Return the Average of the candles
    return sum / static_cast<double>(numCandles);
}

//------------------------------------------
// 4) FETCH CANDLE DATA
//------------------------------------------
nlohmann::json getCandles(
        const std::string& keyName, // Key ID
        const std::string& privateKeyPem, // Private Key
        const std::string& productId, // "BTC-USD"
        const std::string& granularity, // "ONE_MINUTE" or "FIVE_MINUTE"
        int secondsToFetch // 300 for 5 minutes if you want ~5 candles
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

    // Create a signed JWT used as a Bearer token for Coinbase Advanced Trade API authentication
    std::string jwt = create_jwt(keyName, privateKeyPem, method, path);
    // Make Request using httpRequest Function
    // resp then Contains full JSON response (Type String, JSON Format)
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
        return jsonResp["candles"]; // An array of JSON candle object
    }

    return {};
}

//------------------------------------------
// 5) PLACE LIMIT ORDER (MAKER)
//------------------------------------------
bool placeLimitOrder(
        const std::string& keyName, // Key ID
        const std::string& privateKeyPem, // Private Key
        const std::string& productId, // "BTC-USD"
        const std::string& side,     // "BUY" or "SELL"
        double limitPrice,           // Price to Put the Limit Order at
        double quoteAmountUsd,       // How much USD to Use ("$5" Right Now)
        const std::string& clientOrderId // Unique Order ID you create
)
{
    // Endpoint
    // Construct URL
    std::string path = "/api/v3/brokerage/orders";
    std::string method = "POST";
    std::string fullUrl = "https://api.coinbase.com" + path;

    // JSON body
    nlohmann::json orderBody;
    orderBody["client_order_id"] = clientOrderId;
    orderBody["product_id"] = productId;
    orderBody["side"] = side;

    // We want a limit order with GTC (Good Til Canceled)
    nlohmann::json limitGtc;
    // Price as string (Type Required by Coinbase, to Two Decimal Places)
    // Manually Rounding
    double limitPriceRounded = std::floor(limitPrice * 100.0 + 0.5) / 100.0;
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << limitPriceRounded;
    std::string limitPriceStr = oss.str();
    limitGtc["limit_price"] = limitPriceStr;

    // For a limit order, we can do "base_size" or "quote_size".
    // We'll do quote_size for both sides
    limitGtc["quote_size"] = std::to_string(quoteAmountUsd);

    // Make sure filling only Maker Orders to avoid High Taker Fees
    // "post_only" to ensure it's a maker order
    limitGtc["post_only"] = true;

    // Coinbase requires limit orders to be nested inside "order_configuration" with the key "limit_limit_gtc"
    nlohmann::json config;
    config["limit_limit_gtc"] = limitGtc;
    orderBody["order_configuration"] = config;

    // Dump JSON
    // Convert JSON Object to String for HTTP POST Body
    std::string postData = orderBody.dump();

    // Create and Sign JWT (Required Bearer Token)
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
    // Your Key ID and Private Key
    std::string keyName       = std::getenv("KEY_NAME");
    std::string privateKeyPem = std::getenv("PRIVATE_KEY_PEM");

    // What are you trading
    std::string productId = "BTC-USD";

    // For storing the last known state of the short vs long MA
    bool shortWasAbove = false;
    bool shortWasBelow = false;
    bool havePosition = false;  // Are we currently in a long position?

    // Track the fill price of last buy
    double lastBuyPrice = 0.0;

    // We'll run an infinite loop, checking MAs every ~30 seconds
    while (true)
    {
        try {
            // Get short-term MA (1-minute candles)
            // Need at least 5 minutes of 1-minute data. We get ~10 minutes to be safe:
            auto oneMinCandles = getCandles(keyName, privateKeyPem, productId, "ONE_MINUTE", 600 /* 10 min in seconds*/);
            double shortMA = computeMovingAverage(oneMinCandles, 5);

            // Get long-term MA (5-minute candles)
            // Need at least 25 minutes if we wanted 5 periods of 5-minute. We get ~30 minutes to be safe:
            auto fiveMinCandles = getCandles(keyName, privateKeyPem, productId, "FIVE_MINUTE", 1800 /* 30 min in seconds*/);
            double longMA = computeMovingAverage(fiveMinCandles, 5);

            // Error Check
            if (shortMA <= 0.0 || longMA <= 0.0) {
                std::cerr << "[WARN] Could not compute MAs. shortMA=" << shortMA << ", longMA=" << longMA << "\n";
                std::this_thread::sleep_for(std::chrono::seconds(30));
                continue;
            }

            std::cout << "[INFO] shortMA=" << shortMA << ", longMA=" << longMA << std::endl;

            // Check Crossovers
            bool shortAbove = (shortMA > longMA);
            bool shortBelow = (shortMA < longMA);

            // Short MA Below and Stayed Below Long MA
            if (shortBelow && shortWasAbove == false && shortWasBelow == true) {

            }

            // Short MA Above and Was Below Long MA -> BUY (And Wasn't Already in a Position)
            if (shortWasBelow && shortAbove && !havePosition) {
                // Place buy limit order with quote_size = 5 USD, limit price ~ slightly below shortMA

                double limitPrice = shortMA * 0.999; // Multiply by 0.999 to Help Ensure a Maker Order (post_only ensures this)
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
                    lastBuyPrice = shortMA;
                    std::cout << "[STRATEGY] Placed BUY order at limit=" << limitPrice << "\n";
                }
            }

            // If we Have a Position and Short MA < Long MA -> SELL
            if (havePosition && shortMA < longMA) {
                /*
                 * Change coefficient of 'lastBuyPrice' to determine amount profit you want before triggering a sell
                 * This program assumes you are only accounting for the net fees on the trade made.
                 * Note:
                 * Coefficient >= 1
                 * Higher the coefficient higher the profits however,
                 * your position may take longer to sell depending on the market.
                 */

                double minSellPrice = lastBuyPrice * 1.013; // Multiply by 1.013 to cover fees
                if (shortMA >= minSellPrice) {
                    // place SELL
                    double limitPrice = shortMA * 1.001; // Multiply by 1.001 to Help Ensure a Maker Order (post_only ensures this)
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

        // Wait 30 seconds before next iteration
        std::this_thread::sleep_for(std::chrono::seconds(30));
    }

    return 0;
}
