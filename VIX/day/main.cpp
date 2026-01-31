#include <iostream>
#include <fstream>
#include <string>
#include <cmath>
#include <map>
#include <ctime>
#include <sys/stat.h>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
using namespace std;

struct Candle {
    string ts;
    double open;
    double high;
    double low;
    double close;
};

struct Stats {
    int pass = 0;
    int fail = 0;
    int cur_win = 0;
    int cur_loss = 0;
    int max_win = 0;
    int max_loss = 0;
};

/* ================= CURL ================= */

size_t write_cb(void* contents, size_t size, size_t nmemb, string* s) {
    size_t total = size * nmemb;
    s->append((char*)contents, total);
    return total;
}

string http_get(const string& url) {
    CURL* curl = curl_easy_init();
    string response;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    return response;
}

/* ================= DATE → WEEKDAY ================= */

string get_weekday(const string& date) {
    tm t = {};
    strptime(date.c_str(), "%Y-%m-%d", &t);
    mktime(&t);

    static const char* days[] = {
        "Sunday","Monday","Tuesday","Wednesday",
        "Thursday","Friday","Saturday"
    };
    return days[t.tm_wday];
}

/* ================= FIND CANDLE ================= */

Candle find_candle(const json& j, const string& date) {
    for (auto& c : j["data"]["candles"]) {
        string ts = c[0];
        if (ts.substr(0,10) == date) {
            return { ts, c[1], c[2], c[3], c[4] };
        }
    }
    throw runtime_error("Candle not found");
}

/* ================= MAIN ================= */

int main() {
    ifstream f("config.json");
    json cfg;
    f >> cfg;

    string vix_key   = cfg["instrument_key_vix"];
    string nifty_key = cfg["instrument_key_nifty"];
    string start     = cfg["start_date"];
    string end       = cfg["end_date"];

    auto make_url = [&](string key) {
        return "https://api.upstox.com/v3/historical-candle/" +
               string(curl_easy_escape(NULL, key.c_str(), 0)) +
               "/days/1/" + end + "/" + start;
    };

    json vix_json   = json::parse(http_get(make_url(vix_key)));
    json nifty_json = json::parse(http_get(make_url(nifty_key)));

    mkdir("output", 0777);

    map<string, ofstream> csv_files;
    map<string, Stats> stats;

    for (auto& c : nifty_json["data"]["candles"]) {
        string date = c[0].get<string>().substr(0,10);

        try {
            Candle vix   = find_candle(vix_json, date);
            Candle nifty = find_candle(nifty_json, date);

            string day = get_weekday(date);
            string dir = "output/" + day;
            mkdir(dir.c_str(), 0777);

            string csv_path = dir + "/output.csv";

            if (!csv_files.count(day)) {
                csv_files[day].open(csv_path, ios::app);
                csv_files[day]
                    << "DATE,VIX_OPEN,NIFTY_OPEN,LOWER-UPPER,DAY_RANGE,Close,RESULT\n";
            }

            double daily_pct = vix.open / sqrt(252.0);
            double range_pts = nifty.open * (daily_pct / 100.0);

            double lower = nifty.open - range_pts;
            double upper = nifty.open + range_pts;

            bool pass = (nifty.close >= lower && nifty.close <= upper);

            csv_files[day]
                << date << ","
                << vix.open << ","
                << nifty.open << ","
                << nifty.low << "-" << nifty.high << ","
                << lower << "-" << upper << ","
                << nifty.close << ","
                << (pass ? "PASS" : "FAIL") << "\n";

            // ===== STATS UPDATE =====
            if (pass) {
                stats[day].pass++;
                stats[day].cur_win++;
                stats[day].cur_loss = 0;
                stats[day].max_win = max(stats[day].max_win, stats[day].cur_win);
            } else {
                stats[day].fail++;
                stats[day].cur_loss++;
                stats[day].cur_win = 0;
                stats[day].max_loss = max(stats[day].max_loss, stats[day].cur_loss);
            }

        } catch (...) { continue; }
    }

    // ===== WRITE SUMMARY FILES =====
    for (auto& [day, st] : stats) {
        string path = "output/" + day + "/summary.txt";
        ofstream s(path);

        int total = st.pass + st.fail;
        double win_prob = total ? (double)st.pass / total * 100.0 : 0.0;

        s << "TOTAL_DAYS      : " << total << "\n";
        s << "TOTAL_PASS      : " << st.pass << "\n";
        s << "TOTAL_FAIL      : " << st.fail << "\n";
        s << "MAX_WIN_STREAK  : " << st.max_win << "\n";
        s << "MAX_LOSS_STREAK : " << st.max_loss << "\n";
        s << "WIN_PROBABILITY : " << win_prob << " %\n";
        s.close();
    }

    for (auto& p : csv_files)
        p.second.close();

    cout << "✅ Day-wise CSV + summary files generated successfully\n";
    return 0;
}
