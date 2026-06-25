#pragma once

#include "FMWrapper/FMXExtern.h"
#include "FMWrapper/FMXTypes.h"
#include "FMWrapper/FMXCalcEngine.h"
#include "FMWrapper/FMXData.h"

// Each function matches the signature FileMaker expects for external functions.
// Return kNoErr on success; FileMaker will surface any non-zero code as an error.

FMX_PROC(fmx::errcode) Fn_Version     (short funcId, const fmx::ExprEnv& env, const fmx::DataVect& args, fmx::Data& result);
FMX_PROC(fmx::errcode) Fn_Connect     (short funcId, const fmx::ExprEnv& env, const fmx::DataVect& args, fmx::Data& result);
FMX_PROC(fmx::errcode) Fn_Publish     (short funcId, const fmx::ExprEnv& env, const fmx::DataVect& args, fmx::Data& result);
FMX_PROC(fmx::errcode) Fn_Disconnect  (short funcId, const fmx::ExprEnv& env, const fmx::DataVect& args, fmx::Data& result);
FMX_PROC(fmx::errcode) Fn_SetProperty (short funcId, const fmx::ExprEnv& env, const fmx::DataVect& args, fmx::Data& result);
FMX_PROC(fmx::errcode) Fn_DeclareQueue   (short funcId, const fmx::ExprEnv& env, const fmx::DataVect& args, fmx::Data& result);
FMX_PROC(fmx::errcode) Fn_DeclareExchange(short funcId, const fmx::ExprEnv& env, const fmx::DataVect& args, fmx::Data& result);
FMX_PROC(fmx::errcode) Fn_BindQueue      (short funcId, const fmx::ExprEnv& env, const fmx::DataVect& args, fmx::Data& result);
FMX_PROC(fmx::errcode) Fn_TLSTest        (short funcId, const fmx::ExprEnv& env, const fmx::DataVect& args, fmx::Data& result);
