#include <iostream>
#include <fstream>
#include <string>
#include <cmath>
#include <map>
#include <vector>
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
            return {
                ts,
                c[1].get<double>(),
                c[2].get<double>(),
                c[3].get<double>(),
                c[4].get<double>()
            };
        }
    }
    throw runtime_error("Candle not found");
}

/* ================= MAIN ================= */

int main() {

    ifstream f("config.json");
    if (!f.is_open()) return 1;

    json cfg;
    f >> cfg;

    string nifty_key = cfg.value("instrument_key_nifty", "");
    string vix_key   = cfg.value("instrument_key_vix", "");
    string start     = cfg.value("start_date", "");
    string end       = cfg.value("end_date", "");
    string start_day = cfg.value("start_day", "");
    string expiry_day= cfg.value("expiry_day", "");

    if (nifty_key.empty() || vix_key.empty() ||
        start.empty() || end.empty() ||
        start_day.empty() || expiry_day.empty())
        return 1;

    auto make_url = [&](const string& key) {
        return "https://api.upstox.com/v3/historical-candle/" +
               string(curl_easy_escape(NULL, key.c_str(), 0)) +
               "/days/1/" + end + "/" + start;
    };

    json nifty_json = json::parse(http_get(make_url(nifty_key)));
    json vix_json   = json::parse(http_get(make_url(vix_key)));

    mkdir("output", 0777);

    map<string, ofstream> csv_files;
    map<string, Stats> stats;

    // ---- Collect all dates in order (oldest → newest) ----
    vector<string> dates;
    for (auto it = nifty_json["data"]["candles"].rbegin();
         it != nifty_json["data"]["candles"].rend(); ++it) {
        dates.push_back((*it)[0].get<string>().substr(0,10));
    }

    // ---- Process weekly logic ----
    for (size_t i = 0; i < dates.size(); i++) {

        string start_date = dates[i];
        if (get_weekday(start_date) != start_day)
            continue;

        Candle start_nifty = find_candle(nifty_json, start_date);
        Candle start_vix   = find_candle(vix_json, start_date);

        // ---- Find NEXT expiry day AFTER start date ----
        string expiry_date = "";
        for (size_t j = i + 1; j < dates.size(); j++) {
            if (get_weekday(dates[j]) == expiry_day) {
                expiry_date = dates[j];
                break;
            }
        }

        if (expiry_date.empty())
            continue;

        Candle expiry_nifty = find_candle(nifty_json, expiry_date);

        // ---- Weekly VIX range ----
        double weekly_pct = start_vix.open / sqrt(52.0);
        double range_pts  = start_nifty.open * (weekly_pct / 100.0);

        double lower = start_nifty.open - range_pts;
        double upper = start_nifty.open + range_pts;

        bool pass = (expiry_nifty.close >= lower &&
                     expiry_nifty.close <= upper);

        string dir = "output/" + expiry_day;
        mkdir(dir.c_str(), 0777);

        string csv_path = dir + "/output.csv";

        if (!csv_files.count(expiry_day)) {
            csv_files[expiry_day].open(csv_path, ios::app);
            csv_files[expiry_day]
                << "WEEK_START,VIX_OPEN,START_OPEN,LOWER-UPPER,EXPIRY_CLOSE,RESULT\n";
        }

        csv_files[expiry_day]
            << start_date << ","
            << start_vix.open << ","
            << start_nifty.open << ","
            << lower << "-" << upper << ","
            << expiry_nifty.close << ","
            << (pass ? "PASS" : "FAIL") << "\n";

        // ---- STATS ----
        if (pass) {
            stats[expiry_day].pass++;
            stats[expiry_day].cur_win++;
            stats[expiry_day].cur_loss = 0;
            stats[expiry_day].max_win =
                max(stats[expiry_day].max_win, stats[expiry_day].cur_win);
        } else {
            stats[expiry_day].fail++;
            stats[expiry_day].cur_loss++;
            stats[expiry_day].cur_win = 0;
            stats[expiry_day].max_loss =
                max(stats[expiry_day].max_loss, stats[expiry_day].cur_loss);
        }
    }

    // ---- SUMMARY ----
    for (auto& [day, st] : stats) {
        ofstream s("output/" + day + "/summary.txt");

        int total = st.pass + st.fail;
        double win_prob = total ? (double)st.pass / total * 100.0 : 0.0;

        s << "TOTAL_WEEKS     : " << total << "\n";
        s << "TOTAL_PASS      : " << st.pass << "\n";
        s << "TOTAL_FAIL      : " << st.fail << "\n";
        s << "MAX_WIN_STREAK  : " << st.max_win << "\n";
        s << "MAX_LOSS_STREAK : " << st.max_loss << "\n";
        s << "WIN_PROBABILITY : " << win_prob << " %\n";
    }

    for (auto& p : csv_files)
        p.second.close();

    return 0;
}
