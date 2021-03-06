#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/elf.h>

#include <iostream>
#include <sstream>
#include <string>
#include <algorithm>
#include <iterator>
#include <vector>
#include <string.h>

using namespace std;

template <class Container>
void split2(const std::string& str, Container& cont, char delim = ' ')
{
    std::stringstream ss(str);
    std::string token;
    while (std::getline(ss, token, delim)) {
        cont.push_back(token);
    }
}

string concat(vector<string> strs, int index)
{
	string result;
	for (int i = index; i < strs.size(); i++)
		result.append(strs[i]);
	return result;
}

struct KernelDebugMessage
{
  uint32 type;
  union
  {
    struct ExceptionContext *context;
    struct Library *library;
  } message;
};

class AmigaDOSProcessHandler {
private:
    struct Process *child;
	struct Hook hook;
	static ExceptionContext context;
	bool childExists;
	bool childIsRunning;
	bool parentIsAttached;
private:
	static ULONG amigaos_debug_callback (struct Hook *hook, struct Task *currentTask, struct KernelDebugMessage *dbgmsg);

public:
    void init();
    void cleanup();

    APTR loadChildProcess(const char *path, const char *command, const char *arguments);
	APTR attachToProcess(const char *name);
	void detachFromChild();

	void hookOn();
	void hookOff();

	void readTaskContext ();
	void writeTaskContext ();
	void setTraceBit ();
	void unsetTraceBit();

	static uint32 ip () { return context.ip; }

    void go();
    void waitChild();
};

ExceptionContext AmigaDOSProcessHandler::context;

struct DebugIFace *IDebug = 0;
struct MMUIFace *IMMU = 0;

void AmigaDOSProcessHandler::init ()
{
	IDebug = (struct DebugIFace *)IExec->GetInterface ((struct Library *)SysBase, "debug", 1, 0);
	if (!IDebug) {
		return;
	}

	IMMU = (struct MMUIFace *)IExec->GetInterface ((struct Library *)SysBase, "mmu", 1, 0);
	if (!IMMU) {
		return;
	}
}

void AmigaDOSProcessHandler::cleanup ()
{
	if (IDebug)
		IExec->DropInterface((struct Interface *)IDebug);
	IDebug = 0;

	if (IMMU)
		IExec->DropInterface((struct Interface *)IMMU);
	IMMU = 0;
}

APTR AmigaDOSProcessHandler::loadChildProcess (const char *path, const char *command, const char *arguments)
{
	BPTR lock = IDOS->Lock (path, SHARED_LOCK);
	if (!lock) {
		return 0;
	}
	BPTR homelock = IDOS->DupLock (lock);

	BPTR seglist = IDOS->LoadSeg (command);
	
	if (!seglist) {
		IDOS->UnLock(lock);
		return 0;
	}

	IExec->Forbid(); //can we avoid this?

    child = IDOS->CreateNewProcTags(
		NP_Seglist,					seglist,
//		NP_Entry,					foo,
		NP_FreeSeglist,				TRUE,
		NP_Name,					command,
		NP_CurrentDir,				lock,
		NP_ProgramDir,				homelock,
		NP_StackSize,				2000000,
		NP_Cli,						TRUE,
		NP_Child,					TRUE,
		NP_Arguments,				arguments,
		NP_Input,					IDOS->Input(),
		NP_CloseInput,				FALSE,
		NP_Output,					IDOS->Output(), //pipe_get_write_end(),
		NP_CloseOutput,				FALSE,
		NP_Error,					IDOS->ErrorOutput(),
		NP_CloseError,				FALSE,
		NP_NotifyOnDeathSigTask,	IExec->FindTask(NULL),
		TAG_DONE
	);

	if (!child) {
		IExec->Permit();
		return 0;
	} else {
		IExec->SuspendTask ((struct Task *)child, 0L);		
		childExists = true;

		hookOn();
		readTaskContext();
		IExec->Permit();
	}

	APTR handle;
	
	IDOS->GetSegListInfoTags (seglist, 
		GSLI_ElfHandle, &handle,
		TAG_DONE
	);
	
    return handle;
}

ULONG AmigaDOSProcessHandler::amigaos_debug_callback (struct Hook *hook, struct Task *currentTask, struct KernelDebugMessage *dbgmsg)
{
    struct ExecIFace *IExec = (struct ExecIFace *)((struct ExecBase *)SysBase)->MainInterface;

	uint32 traptype = 0;

	/* these are the 4 types of debug msgs: */
	switch (dbgmsg->type)
	{
		case DBHMT_REMTASK:
			IDOS->Printf("REMTASK\n");
			break;

		case DBHMT_EXCEPTION:
		{
			traptype = dbgmsg->message.context->Traptype;

			memcpy (&context, dbgmsg->message.context, sizeof(struct ExceptionContext));
			
			IDOS->Printf("EXCEPTION\n");
			IDOS->Printf("[HOOK] ip = 0x%x\n", context.ip);
			IDOS->Printf("[HOOK} trap = 0x%x\n", context.Traptype);
			
			// struct debug_message *message = IExec->AllocSysObjectTags (ASOT_MESSAGE,
			// 	ASOMSG_Size, sizeof (struct debug_message),
			// 	TAG_DONE
			// );
			
			// if (traptype == 0x700 || traptype == 0xd00)
			// 	message->type = MSGTYPE_TRAP;
			// else
			// 	message->type = MSGTYPE_EXCEPTION;
			
			// IExec->PutMsg (status->_childMessagePort (), (struct Message *)message);
			
			// returning 1 will suspend the task
			return 1;
		}
		case DBHMT_OPENLIB:
		{
			IDOS->Printf("OPENLIB\n");
			// struct debug_message *message = IExec->AllocSysObjectTags (ASOT_MESSAGE,
			// 	ASOMSG_Size, sizeof(struct debug_message),
			// 	TAG_DONE
			// );
			// message->type = MSGTYPE_OPENLIB;
			// message->library = dbgmsg->message.library;
				
			// IExec->PutMsg(status->_childMessagePort (), (struct Message *)message);
			
		}
		break;

		case DBHMT_CLOSELIB:
		{
			IDOS->Printf("CLOSELIB\n");
			// struct debug_message *message = IExec->AllocSysObjectTags(ASOT_MESSAGE,
			// 	ASOMSG_Size, sizeof(struct debug_message),
			// 	TAG_DONE
			// );
			// message->type = MSGTYPE_CLOSELIB;
			// message->library = dbgmsg->message.library;
				
			// IExec->PutMsg(status->childMessagePort (), (struct Message *)message);
		}
		break;

		default:
			break;
	}
	return 0;
}

void AmigaDOSProcessHandler::hookOn ()
{	
    hook.h_Entry = (ULONG (*)())amigaos_debug_callback;
    hook.h_Data =  0; //(APTR)&_hookData;

	if (childExists)
		IDebug->AddDebugHook((struct Task *)child, &hook);
}

void AmigaDOSProcessHandler::hookOff ()
{
	if (childExists)
		IDebug->AddDebugHook((struct Task*)child, 0);
}

APTR AmigaDOSProcessHandler::attachToProcess (const char *name)
{
	struct Process *process = (struct Process *)IExec->FindTask(name);
	if(!process) return 0;

	if (process->pr_Task.tc_Node.ln_Type != NT_PROCESS) {
		return 0;
	}

	BPTR seglist = IDOS->GetProcSegList (process, GPSLF_SEG|GPSLF_RUN);
  
	if (!seglist) {
		return 0;
	}

	if (process->pr_Task.tc_State == TS_READY || process->pr_Task.tc_State == TS_WAIT) {
		IExec->SuspendTask ((struct Task *)process, 0);
//		IExec->Signal ((struct Task *)_me, _eventSignalMask);
	}

	if (process->pr_Task.tc_State == TS_CRASHED) {
		process->pr_Task.tc_State = TS_SUSPENDED;
	}

	child  = process;

	childExists = true;
	childIsRunning = false;
	parentIsAttached = true;
    
	hookOn ();

//	readTaskContext ();

	APTR elfHandle;
	IDOS->GetSegListInfoTags (seglist, 
		GSLI_ElfHandle, &elfHandle,
		TAG_DONE
	);
		
	return elfHandle;
}

void AmigaDOSProcessHandler::detachFromChild ()
{
	if (!childIsRunning)
		go();
	
	hookOff();
	
	childExists = false;
	childIsRunning = false;
	parentIsAttached = false;
}

void AmigaDOSProcessHandler::readTaskContext ()
{
	IDebug->ReadTaskContext  ((struct Task *)child, &context, RTCF_SPECIAL|RTCF_STATE|RTCF_VECTOR|RTCF_FPU);
}

void AmigaDOSProcessHandler::writeTaskContext ()
{
	IDebug->WriteTaskContext ((struct Task *)child, &context, RTCF_SPECIAL|RTCF_STATE|RTCF_VECTOR|RTCF_FPU);
}

#define    MSR_TRACE_ENABLE           0x00000400

void AmigaDOSProcessHandler::setTraceBit ()
{
	struct ExceptionContext ctx;
	IDebug->ReadTaskContext((struct Task *)child, &ctx, RTCF_STATE);
	//this is not supported on the sam cpu:
	ctx.msr |= MSR_TRACE_ENABLE;
	ctx.ip = ip(); //we must reset this because of a system oddity
	IDebug->WriteTaskContext((struct Task *)child, &ctx, RTCF_STATE);
}

void AmigaDOSProcessHandler::unsetTraceBit ()
{
	struct ExceptionContext ctx;
	IDebug->ReadTaskContext ((struct Task *)child, &ctx, RTCF_STATE);
	//this is not supported on the sam cpu:
	ctx.msr &= ~MSR_TRACE_ENABLE;
	ctx.ip = ip();
	IDebug->WriteTaskContext((struct Task *)child, &ctx, RTCF_STATE);
}

void AmigaDOSProcessHandler::go()
{
    IExec->RestartTask((struct Task *)child, 0);
}

void AmigaDOSProcessHandler::waitChild()
{
    IExec->Wait(SIGF_CHILD);
}

vector<string> getInput()
{
	vector<string> cmdArgs;
	
	while(1) {
		cout << "> ";

		string command;
		getline(cin, command);
		split2(command, cmdArgs);

		if(cmdArgs[0].length() == 1)
			break;
	}

	return cmdArgs;
}

int main(int argc, char *argv[])
{
	AmigaDOSProcessHandler handler;
	APTR handle = 0;

	handler.init();

	bool exit = false;
	while(!exit) {
		vector<string> args = getInput();

		switch(args[0][0]) {
			case 'l': {
				string shellArgs;
				if(args.size() >= 3) {
					shellArgs = concat(args, 2);
				}
				if(args.size() >= 2) {
					handle = handler.loadChildProcess("", args[1].c_str(), shellArgs.c_str());
					if (handle) {
						cout << "Child process loaded\n";
					}
				}
				if(args.size() < 2)
					cout << "Too few arguments\n";
			}
				break;

			case 'a': {
				if(args.size() < 2) {
					cout << "Not enough arguments\n";
				} else {
					handle = handler.attachToProcess(args[1].c_str());
					if(handle)
						cout << "Attached to process\n";
				}
			}

			case 'd':
				handler.detachFromChild();
				break;
				
			case 'r':
				handler.readTaskContext();
				break;

			case 'i':
				cout << "ip: " << (void *)handler.ip() << "\n";
				break;

			case 't':
				handler.setTraceBit();
				break;

			case 'u':
				handler.unsetTraceBit();
				break;

			case 's':
				handler.go();
				break;
			
			case 'q':
				exit = true;
				break;

			case 'h':
				cout << "==HELP==\n";
				cout << "l <file> <args>: load child from file\n";
				cout << "a <name>: attach to process in memory\n";
				cout << "d: detach from child\n";
				cout << "s: start execution\n";
				cout << "t: set trace bit\n";
				cout << "u: unset trace bit\n";
				cout << "r: read task context\n";
				cout << "i: print ip\n";
				cout << "q: quit debugger\n";
				break;

			default:
				break;

		}
	}

    if(handle) {
        handler.go();
		handler.waitChild();
    }

	handler.cleanup();

    return 0;
}
