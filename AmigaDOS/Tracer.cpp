#include <proto/exec.h>

#include "Tracer.hpp"
#include "LowLevel.hpp"

#include <cstdint>

Tracer::Tracer(Process *process, ExceptionContext *context) {
    this->process = process;
    this->context = context;
}

void Tracer::activate(bool branching) {
    if(hasTraceBit()) {
        setTraceBit();
    } else {
        breaks.insert(context->ip + 4);
        uint32_t baddr = branchAddress();
        if(baddr && branching)
            breaks.insert(baddr);
        breaks.activate();
    }
}

void Tracer::suspend() {
    if(hasTraceBit()) {
        unsetTraceBit();
    } else {
        breaks.suspend();
        breaks.clear();
    }
}

uint32_t Tracer::branchAddress()
{
	int32 offset;
	switch(PPC_DisassembleBranchInstr(*(uint32 *)context->ip, &offset))
	{
		case PPC_OTHER:
			return 0x0;
		case PPC_BRANCHTOLINK:
		case PPC_BRANCHTOLINKCOND:
			return context->lr;
		case PPC_BRANCHTOCTR:
		case PPC_BRANCHTOCTRCOND:
			return context->ctr;
		case PPC_BRANCH:
		case PPC_BRANCHCOND:
			return context->ip + offset;
	}
	return 0x0;
}

#define    MSR_TRACE_ENABLE           0x00000400

void Tracer::setTraceBit ()
{
	struct ExceptionContext ctx;
	IDebug->ReadTaskContext((struct Task *)process, &ctx, RTCF_STATE);
	//this is not supported on the sam cpu:
	ctx.msr |= MSR_TRACE_ENABLE;
	ctx.ip = context->ip; //we must reset this because of a system oddity
	IDebug->WriteTaskContext((struct Task *)process, &ctx, RTCF_STATE);
}

void Tracer::unsetTraceBit ()
{
	struct ExceptionContext ctx;
	IDebug->ReadTaskContext ((struct Task *)process, &ctx, RTCF_STATE);
	//this is not supported on the sam cpu:
	ctx.msr &= ~MSR_TRACE_ENABLE;
	ctx.ip = context->ip;
	IDebug->WriteTaskContext((struct Task *)process, &ctx, RTCF_STATE);
}

bool Tracer::hasTraceBit()
{
	uint32 family;
	IExec->GetCPUInfoTags(GCIT_Family, &family, TAG_DONE);
	if (family == CPUFAMILY_4XX)
		return false;
	return true;
}