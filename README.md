## Overview

This repository contains a **C++ trading bot** for Coinbase Advanced Trade. It uses moving average crossovers to determine buy/sell actions and places limit maker orders using authenticated JWT-based API requests.

- Implements two moving averages:
   - **Short-Term MA**: Based on 1-minute candles.
   - **Long-Term MA**: Based on 5-minute candles.
- Trades with a small fixed USD amount (e.g., $5) based on crossover logic.
- All orders are posted **maker-only** (`post_only=true`) to minimize taker fees.


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
   - **C++17 compiler**
   - `jwt-cpp` — JWT creation & ES256 signing
   - OpenSSL (`libssl`, `libcrypto`) — cryptography & RAND_bytes
   - `libcurl` — HTTP / HTTPS requests
   - `nlohmann/json` — JSON parsing
   - `pthread` (Linux) — required by jwt-cpp / OpenSSL
Make sure these libraries are installed and linked when building.

## Security & Credentials
The bot never embeds secrets in source code. 
Instead it reads your **Coinbase Advanced Trade API key** and **EC private key** from environment variables:
```cpp
std::string keyName       = std::getenv("KEY_NAME");       // Your Key ID (In Coinbase Api Key: "id")
std::string privateKeyPem = std::getenv("PRIVATE_KEY_PEM"); // Your Private Key (In Coinbase Api Key: "privateKey")
```
The code also injects a 16-byte nonce into the JWT header to prevent replay attacks.
### Example (Bash)
```bash
export KEY_NAME="your_api_key_name"
export PRIVATE_KEY_PEM=$'-----BEGIN EC PRIVATE KEY-----\n...\n-----END EC PRIVATE KEY-----'
```
Before running:
1. Set both variables in your shell, `.env`, or a secret manager.
2. Never commit the PEM string to Git (it grants signing authority).
3. Rotate keys in Coinbase if you suspect they were exposed.

## How to Build & Run
### Build
1. Clone this repository.
2. Install dependencies.
3. Use your preferred C++ build system (e.g., `g++`, `CMake`) to compile the program.
```bash
g++ -std=c++17 main.cpp -o trading_bot -lcurl -lssl -lcrypto -pthread
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

