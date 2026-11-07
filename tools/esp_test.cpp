#include "func/esp/esp.h"
#include <cstdio>
int main() {
    ImGui::CreateContext();
    auto& io = ImGui::GetIO(); io.IniFilename=nullptr;
    io.DisplaySize={1920,1080}; io.DeltaTime=1.0f/60;
    unsigned char* pixels; int width,height;
    io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
    GameFrame frame;
    frame.view.m[0][1]=-1; frame.view.m[1][2]=16.0f/9;
    frame.view.m[3][0]=1;
    EspPlayer player; player.origin={500,0,-30}; player.health=100;
    player.bones_valid=false;
    frame.players.push_back(player);
    Settings settings; settings.enemy.enabled=true;
    settings.enemy.health=true; settings.enemy.skeleton=true; settings.enemy.boxes=true;
    ImGui::NewFrame();
    DrawEsp(frame,settings,{0,0,1920,1080},nullptr,14,nullptr,14);
    const bool ok=ImGui::GetBackgroundDrawList()->VtxBuffer.Size>0;
    ImGui::EndFrame();
    frame.players[0].origin={-500,0,-30};
    settings.enemy.offscreen=true;
    ImGui::NewFrame();
    DrawEsp(frame,settings,{0,0,1920,1080},nullptr,14,nullptr,14);
    const bool behind=ImGui::GetBackgroundDrawList()->VtxBuffer.Size>0;
    ImGui::EndFrame();settings.enemy.offscreen=false;
    ImGui::NewFrame();
    DrawEsp(frame,settings,{0,0,1920,1080},nullptr,14,nullptr,14);
    const bool disabled=ImGui::GetBackgroundDrawList()->VtxBuffer.Size==0;
    ImGui::EndFrame();settings.enemy.offscreen=true;settings.enemy.visible_only=true;
    ImGui::NewFrame();
    DrawEsp(frame,settings,{0,0,1920,1080},nullptr,14,nullptr,14);
    const bool filtered=ImGui::GetBackgroundDrawList()->VtxBuffer.Size==0;
    ImGui::EndFrame();ImGui::DestroyContext();
    std::printf("ESP without bones: %s\n",ok?"pass":"FAIL");
    std::printf("offscreen integration: %s\n",behind&&disabled&&filtered?"pass":"FAIL");
    return !(ok&&behind&&disabled&&filtered);
}
