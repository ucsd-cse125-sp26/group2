#pragma once

#include <string>

struct ConfirmRequest
{
    std::string title;
    std::string message;
    std::string confirmText;
    std::string cancelText;
    bool confirmIsDanger = true;
};

enum class ConfirmResult
{
    Pending,
    Confirmed,
    Cancelled,
};

class ConfirmModal
{
public:
    void open(ConfirmRequest request);
    void cancel();
    [[nodiscard]] bool isOpen() const;
    ConfirmResult drawAndPoll();

private:
    ConfirmRequest request_;
    bool open_ = false;
    bool shouldOpenPopup_ = false;
};
