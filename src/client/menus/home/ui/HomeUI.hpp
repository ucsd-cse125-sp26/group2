#pragma once

struct JoinMenuState
{
    char serverIp[64] = "127.0.0.1";
    int serverPort = 9999;
};

struct JoinMenuResult
{
    bool connectClicked = false;
};

namespace home_ui
{
JoinMenuResult buildJoinMenu(JoinMenuState& state);
}
