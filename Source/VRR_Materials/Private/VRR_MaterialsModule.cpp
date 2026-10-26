/*
 * VRR_Materials runtime module.
 *
 * Holds the VRR material nodes (MF_*: UMaterialExpressionVrr*, all built on UMaterialExpressionVrrBase and the
 * VrrMaterialMath helpers) and the toon shadow caster actor. Nothing needs to run at startup: the node classes are
 * found by reflection, so the default module implementation is enough.
 */

#include "Modules/ModuleManager.h"

IMPLEMENT_MODULE(FDefaultModuleImpl, VRR_Materials)
