#include "GarmentRegistry.h"
#include "DressModule.h"
#include "ShirtModule.h"
#include "PantsModule.h"
#include "CoatModule.h"
#include "SuitJacketModule.h"
#include "SuitModule.h"
#include "OuterwearModules.h"
#include "OnePieceModules.h"
#include "SkirtCapeVestModules.h"

namespace pf {

std::vector<GarmentModulePtr> allGarmentModules() {
    return {
        std::make_shared<DressModule>(),
        std::make_shared<CoatModule>(),
        std::make_shared<OvercoatModule>(),
        std::make_shared<PeacoatModule>(),
        std::make_shared<TrenchCoatModule>(),
        std::make_shared<SuitJacketModule>(),
        std::make_shared<SuitModule>(),
        std::make_shared<JumpsuitModule>(),
        std::make_shared<BodysuitModule>(),
        std::make_shared<ShirtModule>(),
        std::make_shared<SkirtModule>(),
        std::make_shared<CapeModule>(),
        std::make_shared<VestModule>(),
        std::make_shared<PantsModule>(),
    };
}

} // namespace pf
