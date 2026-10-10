#include "../src/cwsdk-extension/helper/AquaticRules.h"
#include "../src/cwsdk-extension/helper/WaterPopulationRules.h"
#include <cassert>
#include <cstdio>
int main(){
 for(unsigned h=0;h<600;h++){int r=LakeDominantRace(h);assert(LakeEncounterRace(r));assert(!AquaticPassive(r));assert(r==LakeDominantRace(h));}
 assert(LakeDominantRace(0)==106&&LakeDominantRace(1)==86&&LakeDominantRace(2)==37);
 for(int r:{75,107,145,150,31})assert(!LakeEncounterRace(r));
 for(int id:{145,146,147,152,155,295})assert(AquaticPassive(id));
 for(int id:{75,106,107,148,149,150,153,154})assert(!AquaticPassive(id));
 for(auto boss:AquaticBosses){assert(!AquaticPassive(boss.race));assert(boss.race==75||boss.race==150||boss.race==153||boss.race==154||boss.race==107);assert(boss.scale<=2.5);}
 SwimPoint a{-100000000.25,-17000000.75,25},b{a.x+8,a.y+6,27};
 auto next=SwimStep(a,b,.016,1.3);assert(SwimDistance(a,next)<=1.3*.016+1e-7);
 assert(SwimDistance(a,SwimStep(a,b,10,1.3))<=.13+1e-7);
 assert(SwimDistance(b,SwimStep(a,b,.1,1000))<1e-8);
 assert(SwimPathClear(a,b,[](SwimPoint){return true;}));
 assert(!SwimPathClear(a,b,[&](SwimPoint p){return std::abs(p.x-(a.x+4))>.3;}));
 assert(!SwimPathClear(a,{a.x+30,a.y,a.z},[](SwimPoint){return true;}));
 assert(!SwimPathClear(a,b,[&](SwimPoint p){return p.z<26;}));
 assert(WaterPopulationCap==18&&WaterSchoolMax==3);
 assert(WaterWithinRadius(-1000000,-1000000,-1000000,-1000000,192));
 assert(!WaterWithinRadius(-999000,-1000000,-1000000,-1000000,192));
 assert(WaterWithinRadius(192,0,0,0,192)&&!WaterWithinRadius(193,0,0,0,192));
 assert(WaterEntityLifecycle(true,true,false,true,false)==WaterEntityAction::Retire);
 assert(!WaterPopulationCanSpawn(60,0,100,0));
 SwimPoint recovered{};
 assert(SwimRecoverDepth({0,0,5},&recovered,[](SwimPoint){return true;},[](SwimPoint p){return p.z<=1;}));assert(recovered.z==1);
 assert(!SwimRecoverDepth({0,0,5},&recovered,[](SwimPoint p){return p.z>2;},[](SwimPoint p){return p.z<=1;}));
 assert(!SwimRecoverDepth({0,0,50},&recovered,[](SwimPoint){return true;},[](SwimPoint p){return p.z<=1;}));
 puts("Species temperament, aquatic-only hostile bosses, smooth bounded movement, wall/surface/unloaded-path rejection and population lifecycle: PASS");
}
