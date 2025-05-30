## Overview

This repository contains a **C++ trading bot** for Coinbase Advanced Trade. It uses moving average crossovers to determine buy/sell actions and places limit maker orders using authenticated JWT-based API requests.

- Implements two moving averages:
   - **Short-Term MA**: Based on 1-minute candles.
   - **Long-Term MA**: Based on 5-minute candles.
- Trades with a small fixed USD amount (e.g., $5) based on crossover logic.


## Project Structure and Usage

```text
coinbase-trading-bot/
├── main.cpp
├── CMakeLists.txt (if applicable)
├── README.md
└── ...
```

1. `main.cpp`
   - Implements:
     - JWT creation using `jwt-cpp` and OpenSSL.
     - Authenticated HTTP requests with `libcurl`.
     - Candle fetching, MA calculations, and basic crossover trading logic.
   - Continuously loops with a 30-second delay to monitor market conditions and place orders as needed.
  
## Dependencies
This bot uses the following C++ libraries:
   - `jwt-cpp` — for signing JWTs
   - `OpenSSL` — for cryptographic functions (`RAND_bytes`)
   - `libcurl` — for HTTP requests
   - `nlohmann/json` — for parsing Coinbase's JSON API responses
Make sure these libraries are installed and linked when building.

## Security & Credentials
Sensitive API credentials are now retrieved securely using environment variables. Make sure the following environment variables are set before running:
```java
std::string keyName       = std::getenv("KEY_NAME");
std::string privateKeyPem = std::getenv("PRIVATE_KEY_PEM");
```
These values must be defined in your shell or .env file and must not be committed to your repository.
### Example (Bash)
```bash
export KEY_NAME="your_api_key_name"
export PRIVATE_KEY_PEM="-----BEGIN EC PRIVATE KEY-----\n...\n-----END EC PRIVATE KEY-----"
```
Before running:
1. Set the environment variables with your actual Coinbase Advanced Trade API credentials.
2. Use a secure method (like .env files or credential managers) to manage secrets.

## How to Build & Run
### Build
1. Clone this repository.
2. Install dependencies.
3. Use your preferred C++ build system (e.g., `g++`, `CMake`) to compile the program.
```bash
g++ main.cpp -o trading_bot -lcurl -lssl -lcrypto
```
### Run
```bash
./trading_bot
```

## Strategy Logic
1. **Buy Condition**:
   - Short-term MA crosses above long-term MA.
   - You are not already holding a position.
2. **Sell Condition**:
   - Short-term MA falls below long-term MA.
   - You are holding a position.
   - Current price is sufficiently above the last buy price to cover fees and earn profit (default multiplier is 1.013).
  
## Output & Logs
During execution, the bot prints logs such as:
   - Current short and long MAs
   - Placed buy/sell orders
   - Error messages (e.g., failed JSON parse, HTTP failures)
These are written to `stdout` for real-time monitoring.

## Customization
   - **Trade Amount**: Modify `quoteAmountUsd` in `placeLimitOrder()`.
   - **Profit Threshold**: Adjust `minSellPrice = lastBuyPrice * 1.013`.
   - **Sleep Interval**: Change the `std::this_thread::sleep_for(...)` value in the main loop.

## Common Issues
1. **JWT creation fails**: Usually caused by an invalid private key or malformed JWT structure.
2. **HTTP 401/403 responses**: Ensure your credentials are correct and that JWTs are signed properly.
3. **libcurl errors**: Check network connection or API rate limits.
4. **JSON parsing issues**: Validate Coinbase API hasn’t changed response structure.

