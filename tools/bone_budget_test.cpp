#include "core/bone_scan_budget.h"
#include <cstdio>
int main() {
    int failures=0;
    auto check=[&](bool ok,const char* label) { std::printf("%s %s\n",ok?"pass":"FAIL",label);failures+=!ok; };
    BoneScanBudget budget;
    check(budget.Acquire(0,1) && !budget.Acquire(0,2),"one layout scan per frame");
    budget.BeginFrame();
    check(!budget.Acquire(1,2),"global cooldown");
    check(!budget.Acquire(50,1) && budget.Acquire(50,2),"bad first player cannot starve second");
    budget.BeginFrame();
    check(budget.Acquire(250,1),"same player can retry after cooldown");
    budget=BoneScanBudget{};
    int searches=0;
    for(int now=0;now<1000;++now) {
        budget.BeginFrame();
        for(int slot=1;slot<=64;++slot) searches+=budget.Acquire(now,slot);
    }
    check(searches==20,"64 invalid players at 1000 FPS still cause only 20 searches per second");
    return failures!=0;
}
