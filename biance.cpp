#include <ixwebsocket/IXWebSocket.h>
#include <iostream>
#include <unistd.h>
#include <string>
#include <nlohmann/json.hpp>

using namespace std;
using json=nlohmann::json;

int main(){
    ix::WebSocket ws;

    ws.setUrl("wss://stream.binance.com:9443/ws/btcusdt@trade");

    ws.setOnMessageCallback(
        [&](const ix::WebSocketMessagePtr& msg)
        {
            if(msg->type==ix::WebSocketMessageType::Open)
            {
                cout<<"Websocket Connected Successfully"<<endl;
            }

            else if(msg->type==ix::WebSocketMessageType::Message)
            {
                try
                {
                    json j=json::parse(msg->str);

                    if(j.contains("result") && j.contains("id"))
                    {
                        return;
                    }

                    else if(j.contains("e") && j["e"]=="trade")
                    {
                        string symbol=j["s"].get<string>();
                        double price=stod(j["p"].get<string>());

                        cout<<"Symbol : "<<symbol
                            <<" | Price : "<<price<<endl;
                    }
                }
                catch(exception& e)
                {
                    cout<<"Erro : "<<e.what()<<endl;
                }
                
            }

        }
    );

    ws.start();

    while(true)
    {
        usleep(500);
    }

    ws.stop();

    return 0;
}
