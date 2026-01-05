#include <ixwebsocket/IXWebSocket.h>
#include <iostream>
#include <unistd.h>
#include <string>
#include <chrono>
#include <mutex>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

using namespace std;
using json = nlohmann::json;

/* ================= TELEGRAM CONFIG ================= */
const string BOT_TOKEN = "8031357753:AAF75pKyoKQNPi_q6bw2PsV8aeoPqjAV4y8";
const string CHANNEL   = "@testing505050";

/* ================= GLOBAL STATE ================= */
mutex data_mtx;
string latest_symbol;
double latest_price = 0.0;

chrono::steady_clock::time_point last_sent;

/* ================= TELEGRAM SEND ================= */
void send_telegram(const string& text)
{
    CURL* curl = curl_easy_init();
    if(!curl) return;

    char* esc = curl_easy_escape(curl, text.c_str(), text.length());

    string url =
        "https://api.telegram.org/bot" + BOT_TOKEN +
        "/sendMessage?chat_id=" + CHANNEL +
        "&text=" + esc;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    curl_easy_perform(curl);

    curl_free(esc);
    curl_easy_cleanup(curl);
}

/* ================= MAIN ================= */
int main()
{
    ix::WebSocket ws;
    ws.setUrl("wss://stream.binance.com:9443/ws/btcusdt@trade");

    ws.setOnMessageCallback(
        [&](const ix::WebSocketMessagePtr& msg)
        {
            if(msg->type == ix::WebSocketMessageType::Open)
            {
                cout << "WebSocket Connected" << endl;
            }
            else if(msg->type == ix::WebSocketMessageType::Message)
            {
                try
                {
                    json j = json::parse(msg->str);

                    if(j.contains("e") && j["e"] == "trade")
                    {
                        lock_guard<mutex> lock(data_mtx);

                        latest_symbol = j["s"].get<string>();
                        latest_price  = stod(j["p"].get<string>());
                    }
                }
                catch(const exception& e)
                {
                    cout << "JSON error: " << e.what() << endl;
                }
            }
        }
    );

    ws.start();

    /* ===== TELEGRAM LOOP (1 SEC) ===== */
    while(true)
    {
        this_thread::sleep_for(chrono::seconds(1));

        string symbol;
        double price;

        {
            lock_guard<mutex> lock(data_mtx);
            symbol = latest_symbol;
            price  = latest_price;
        }

        if(symbol.empty()) continue;

        json out;
        out["symbol"] = symbol;
        out["price"]  = price;
        out["time"]   = time(nullptr);

        send_telegram(out.dump());
        cout << "Sent to Telegram: " << out.dump() << endl;
    }

    ws.stop();
    return 0;
}
