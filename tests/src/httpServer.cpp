// http_server.cpp
#include "TcpServer.hpp"
#include "logger.hpp"
#include <atomic>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <regex>
#include <fstream>
#include <iostream>
#include <csignal>

using namespace std;

// 全局变量，用于信号处理
atomic<bool> g_running{true};

void signal_handler(int signum) {
    cout << "\nReceived signal " << signum << ", shutting down..." << endl;
    g_running = false;
}

class HttpServer {
public:
    struct Stats {
        atomic<uint64_t> total_requests{0};
        atomic<uint64_t> total_bytes_received{0};
        atomic<uint64_t> total_bytes_sent{0};
        atomic<uint64_t> active_connections{0};
        atomic<uint64_t> error_requests{0};
        
        void reset() {
            total_requests = 0;
            total_bytes_received = 0;
            total_bytes_sent = 0;
            active_connections = 0;
            error_requests = 0;
        }
    };
    
    HttpServer(EventLoop* loop, 
               const string& ip, 
               uint16_t port, 
               int threads = thread::hardware_concurrency(),
               const string& name = "HttpServer")
        : server_(loop, ip, port, threads, name),
          stats_() {
        
        // 设置回调
        server_.set_connection_callback(
            bind(&HttpServer::onConnection, this, placeholders::_1));
        server_.set_message_callback(
            bind(&HttpServer::onMessage, this, placeholders::_1, placeholders::_2));
        
        // 启用空闲连接超时（10分钟）
        server_.enable_idle_timeout(true);
        server_.set_idle_timeout(600000);
        
        // 设置默认HTTP路由
        setup_routes();
        
        LOG_INFO("[HttpServer] Server created: %s:%d, threads=%d", 
                 ip.c_str(), port, threads);
    }
    
    void start() {
        server_.start();
        LOG_INFO("[HttpServer] Server started");
        
        // 启动统计输出线程
        stats_thread_ = thread([this] {
            while (!stop_stats_thread_) {
                this_thread::sleep_for(chrono::seconds(10));
                print_stats();
            }
        });
    }
    
    void stop() {
        stop_stats_thread_ = true;
        if (stats_thread_.joinable()) {
            stats_thread_.join();
        }
        server_.stop();
        LOG_INFO("[HttpServer] Server stopped");
    }
    
    const Stats& get_stats() const { return stats_; }
    
    TcpServer& get_server() { return server_; }
    
private:
    void onConnection(const TcpConnection::Ptr& conn) {
        if (conn->is_connected()) {  // 修改为 is_connected()
            stats_.active_connections++;
            LOG_DEBUG("[HttpServer] New connection: fd=%d, peer=%s", 
                      conn->fd(), conn->peer_ipport().c_str());
        } else {
            stats_.active_connections--;
            LOG_DEBUG("[HttpServer] Connection closed: fd=%d", conn->fd());
        }
    }
    
    void onMessage(const TcpConnection::Ptr& conn, InputBuffer& buffer) {
        stats_.total_requests++;
        
        // 读取HTTP请求
        string request(buffer.get_from_buf(), buffer.length());
        stats_.total_bytes_received += request.size();
        
        // 解析HTTP请求
        HttpRequest req = parse_http_request(request);
        
        // 处理请求
        HttpResponse res = handle_request(req);
        
        // 发送响应
        string response_str = build_http_response(res);
        conn->send(response_str);
        
        stats_.total_bytes_sent += response_str.size();
        
        // 清除已处理的数据 - 使用 pop() 而不是 retrieve()
        buffer.pop(request.size());
        
        LOG_DEBUG("[HttpServer] Request: %s %s -> %d", 
                  req.method.c_str(), req.path.c_str(), res.status_code);
    }
    
    struct HttpRequest {
        string method;
        string path;
        unordered_map<string, string> headers;
        string body;
        string query_string;
    };
    
    struct HttpResponse {
        int status_code = 200;
        string status_text = "OK";
        unordered_map<string, string> headers;
        string body;
    };
    
    HttpRequest parse_http_request(const string& request) {
        HttpRequest req;
        istringstream stream(request);
        string line;
        
        // 解析请求行
        if (getline(stream, line)) {
            istringstream line_stream(line);
            string full_path;
            line_stream >> req.method >> full_path >> ws;
            
            // 分离路径和查询字符串
            size_t qmark = full_path.find('?');
            if (qmark != string::npos) {
                req.path = full_path.substr(0, qmark);
                req.query_string = full_path.substr(qmark + 1);
            } else {
                req.path = full_path;
            }
        }
        
        // 解析头部
        while (getline(stream, line) && line != "\r" && line != "") {
            size_t colon_pos = line.find(':');
            if (colon_pos != string::npos) {
                string key = line.substr(0, colon_pos);
                string value = line.substr(colon_pos + 1);
                
                // 去除空白字符
                while (!value.empty() && (value[0] == ' ' || value[0] == '\t')) {
                    value.erase(0, 1);
                }
                if (!value.empty() && value.back() == '\r') {
                    value.pop_back();
                }
                req.headers[key] = value;
            }
        }
        
        // 解析body（如果有）
        if (req.headers.count("Content-Length")) {
            try {
                int content_length = stoi(req.headers["Content-Length"]);
                if (content_length > 0) {
                    vector<char> buffer(content_length);
                    stream.read(buffer.data(), content_length);
                    req.body.assign(buffer.data(), content_length);
                }
            } catch (const exception& e) {
                LOG_ERROR("[HttpServer] Failed to parse Content-Length: %s", e.what());
            }
        }
        
        return req;
    }
    
    void setup_routes() {
        // 添加路由处理函数
        routes_["/"] = [this](const HttpRequest& req) {
            return handle_root(req);
        };
        
        routes_["/benchmark"] = [this](const HttpRequest& req) {
            return handle_benchmark(req);
        };
        
        routes_["/stats"] = [this](const HttpRequest& req) {
            return handle_stats(req);
        };
        
        routes_["/echo"] = [this](const HttpRequest& req) {
            return handle_echo(req);
        };
        
        routes_["/delay"] = [this](const HttpRequest& req) {
            return handle_delay(req);
        };
    }
    
    HttpResponse handle_request(const HttpRequest& req) {
        HttpResponse res;
        
        try {
            // 添加默认头部
            res.headers["Server"] = "Custom-Network-Lib/1.0";
            res.headers["Date"] = get_http_date();
            res.headers["Connection"] = "keep-alive";
            
            // 路由处理
            auto it = routes_.find(req.path);
            if (it != routes_.end()) {
                res = it->second(req);
            } else {
                // 尝试静态文件服务
                res = handle_static_file(req);
            }
        } catch (const exception& e) {
            LOG_ERROR("[HttpServer] Error handling request: %s", e.what());
            res.status_code = 500;
            res.status_text = "Internal Server Error";
            res.body = "500 Internal Server Error\n";
            stats_.error_requests++;
        }
        
        // 设置Content-Length
        res.headers["Content-Length"] = to_string(res.body.size());
        
        // 设置Content-Type如果没有设置
        if (res.headers.find("Content-Type") == res.headers.end()) {
            res.headers["Content-Type"] = "text/plain; charset=utf-8";
        }
        
        return res;
    }
    
    HttpResponse handle_root(const HttpRequest& req) {
        HttpResponse res;
        res.status_code = 200;
        res.status_text = "OK";
        res.headers["Content-Type"] = "text/html; charset=utf-8";
        
        ostringstream html;
        html << "<!DOCTYPE html>\n"
             << "<html>\n"
             << "<head><title>Network Library Test Server</title></head>\n"
             << "<body>\n"
             << "<h1>Network Library Test Server</h1>\n"
             << "<p>Server is running!</p>\n"
             << "<ul>\n"
             << "<li><a href=\"/stats\">Server Statistics</a></li>\n"
             << "<li><a href=\"/benchmark\">Benchmark Endpoint</a></li>\n"
             << "<li><a href=\"/echo\">Echo Endpoint</a></li>\n"
             << "<li><a href=\"/delay?ms=100\">Delay Test (100ms)</a></li>\n"
             << "</ul>\n"
             << "<p>Current time: " << chrono::system_clock::now().time_since_epoch().count() << "</p>\n"
             << "</body>\n"
             << "</html>\n";
        
        res.body = html.str();
        return res;
    }
    
    HttpResponse handle_benchmark(const HttpRequest& req) {
        HttpResponse res;
        res.status_code = 200;
        res.status_text = "OK";
        res.headers["Content-Type"] = "text/plain; charset=utf-8";
        
        ostringstream body;
        body << "Benchmark Endpoint\n"
             << "==================\n"
             << "Time: " << chrono::system_clock::now().time_since_epoch().count() << "\n"
             << "Requests processed: " << stats_.total_requests.load() << "\n"
             << "Active connections: " << stats_.active_connections.load() << "\n"
             << "Bytes received: " << stats_.total_bytes_received.load() << "\n"
             << "Bytes sent: " << stats_.total_bytes_sent.load() << "\n";
        
        res.body = body.str();
        return res;
    }
    
    HttpResponse handle_stats(const HttpRequest& req) {
        HttpResponse res;
        res.status_code = 200;
        res.status_text = "OK";
        res.headers["Content-Type"] = "application/json; charset=utf-8";
        
        ostringstream json;
        json << "{\n"
             << "  \"total_requests\": " << stats_.total_requests.load() << ",\n"
             << "  \"total_bytes_received\": " << stats_.total_bytes_received.load() << ",\n"
             << "  \"total_bytes_sent\": " << stats_.total_bytes_sent.load() << ",\n"
             << "  \"active_connections\": " << stats_.active_connections.load() << ",\n"
             << "  \"error_requests\": " << stats_.error_requests.load() << ",\n"
             << "  \"server_connections\": " << server_.connection_count() << ",\n"
             << "  \"idle_connections\": " << server_.idle_connection_count() << ",\n"
             << "  \"timestamp\": " << chrono::duration_cast<chrono::milliseconds>(
                    chrono::system_clock::now().time_since_epoch()).count() << "\n"
             << "}\n";
        
        res.body = json.str();
        return res;
    }
    
    HttpResponse handle_echo(const HttpRequest& req) {
        HttpResponse res;
        res.status_code = 200;
        res.status_text = "OK";
        res.headers["Content-Type"] = "text/plain; charset=utf-8";
        
        if (req.method == "POST" || req.method == "PUT") {
            res.body = "Echo: " + req.body + "\n";
        } else {
            res.body = "Echo endpoint. Use POST or PUT with data to echo.\n";
        }
        
        return res;
    }
    
    HttpResponse handle_delay(const HttpRequest& req) {
        HttpResponse res;
        res.status_code = 200;
        res.status_text = "OK";
        res.headers["Content-Type"] = "text/plain; charset=utf-8";
        
        // 解析延迟参数
        int delay_ms = 100; // 默认100ms
        
        if (!req.query_string.empty()) {
            regex pattern("ms=([0-9]+)");
            smatch match;
            if (regex_search(req.query_string, match, pattern) && match.size() > 1) {
                try {
                    delay_ms = stoi(match[1]);
                    delay_ms = min(max(delay_ms, 1), 10000); // 限制1ms-10s
                } catch (...) {
                    // 保持默认值
                }
            }
        }
        
        // 模拟延迟
        this_thread::sleep_for(chrono::milliseconds(delay_ms));
        
        ostringstream body;
        body << "Delayed response after " << delay_ms << "ms\n";
        res.body = body.str();
        
        return res;
    }
    
    HttpResponse handle_static_file(const HttpRequest& req) {
        HttpResponse res;
        
        // 简单的静态文件服务（仅用于测试）
        if (req.path.find("..") != string::npos) {
            // 防止目录遍历攻击
            res.status_code = 403;
            res.status_text = "Forbidden";
            res.body = "403 Forbidden\n";
            return res;
        }
        
        string file_path = "./www" + req.path; // 假设有www目录
        if (req.path == "/" || req.path == "") {
            file_path = "./www/index.html";
        }
        
        ifstream file(file_path, ios::binary);
        if (!file.is_open()) {
            res.status_code = 404;
            res.status_text = "Not Found";
            res.body = "404 Not Found\n";
            return res;
        }
        
        // 读取文件内容
        string content((istreambuf_iterator<char>(file)), istreambuf_iterator<char>());
        
        res.status_code = 200;
        res.status_text = "OK";
        
        // 根据文件扩展名设置Content-Type
        size_t dot_pos = file_path.find_last_of('.');
        if (dot_pos != string::npos) {
            string ext = file_path.substr(dot_pos + 1);
            if (ext == "html" || ext == "htm") {
                res.headers["Content-Type"] = "text/html; charset=utf-8";
            } else if (ext == "css") {
                res.headers["Content-Type"] = "text/css; charset=utf-8";
            } else if (ext == "js") {
                res.headers["Content-Type"] = "application/javascript; charset=utf-8";
            } else if (ext == "png") {
                res.headers["Content-Type"] = "image/png";
            } else if (ext == "jpg" || ext == "jpeg") {
                res.headers["Content-Type"] = "image/jpeg";
            } else {
                res.headers["Content-Type"] = "application/octet-stream";
            }
        }
        
        res.body = content;
        return res;
    }
    
    string build_http_response(const HttpResponse& res) {
        ostringstream response;
        
        // 状态行
        response << "HTTP/1.1 " << res.status_code << " " << res.status_text << "\r\n";
        
        // 头部
        for (const auto& [key, value] : res.headers) {
            response << key << ": " << value << "\r\n";
        }
        
        // 空行分隔头部和body
        response << "\r\n";
        
        // body
        response << res.body;
        
        return response.str();
    }
    
    string get_http_date() {
        auto now = chrono::system_clock::now();
        auto now_time = chrono::system_clock::to_time_t(now);
        
        char buf[100];
        strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", gmtime(&now_time));
        return string(buf);
    }
    
    void print_stats() {
        LOG_INFO("[HttpServer] Statistics:");
        LOG_INFO("  Requests: %lu (errors: %lu)", 
                 stats_.total_requests.load(), stats_.error_requests.load());
        LOG_INFO("  Bytes: RX=%lu, TX=%lu", 
                 stats_.total_bytes_received.load(), stats_.total_bytes_sent.load());
        LOG_INFO("  Connections: active=%lu, total=%zu, idle=%zu", 
                 stats_.active_connections.load(), 
                 server_.connection_count(),
                 server_.idle_connection_count());
        LOG_INFO("  Throughput: %.2f req/sec", 
                 stats_.total_requests.load() / 10.0); // 10秒间隔
    }
    
private:
    TcpServer server_;
    Stats stats_;
    thread stats_thread_;
    atomic<bool> stop_stats_thread_{false};
    
    // 路由表
    unordered_map<string, function<HttpResponse(const HttpRequest&)>> routes_;
};

int main(int argc, char* argv[]) {
    try {
        signal(SIGINT, signal_handler);
        signal(SIGTERM, signal_handler);

        // 参数解析
        string ip = "0.0.0.0";
        uint16_t port = 8080;
        int threads = thread::hardware_concurrency();

        if (argc >= 2) port = static_cast<uint16_t>(stoi(argv[1]));
        if (argc >= 3) threads = stoi(argv[2]);
        if (argc >= 4) ip = argv[3];

        // 日志初始化
        logger::Logger::Config log_config;
        log_config.filename = "httpserver.log";
        log_config.level = logger::Logger::Level::INFO;
        log_config.async = true;
        log_config.queue_capacity = 10000;

        if (!logger::Logger::instance().initialize(log_config)) {
            cerr << "Failed to initialize logger!" << endl;
            return 1;
        }

        LOG_INFO("Starting HTTP Server: %s:%d threads=%d",
                 ip.c_str(), port, threads);

        // ===== 核心对象 =====
        EventLoop main_loop;
        HttpServer server(&main_loop, ip, port, threads);

        server.start();

        // ===== 阻塞在这里 =====
        main_loop.loop();

        // ===== loop 退出后，开始清理 =====
        LOG_INFO("EventLoop exited, shutting down server...");
        server.stop();

        logger::Logger::instance().shutdown();
        LOG_INFO("Server exited cleanly");
        return 0;
    }
    catch (const std::exception& e) {
        LOG_ERROR("uncaught exception: %s", e.what());
        abort();
    }
}
