#pragma once

#include <string>
#include <vector>
#include <utility>

class LlmClient {
public:
    LlmClient();

    void set_api_key(const std::string& key);
    void set_api_url(const std::string& url);
    void set_model(const std::string& model);
    void set_system_prompt(const std::string& prompt);

    std::string chat(const std::string& user_message);
    void clear_history();

private:
    std::string api_key_;
    std::string api_url_;
    std::string model_;
    std::string system_prompt_;

    // 对话历史：pair<role, content>
    std::vector<std::pair<std::string, std::string>> history_;

    std::string build_request_body(const std::string& user_message) const;
    std::string parse_response(const std::string& response_body) const;
};
