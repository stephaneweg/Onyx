//
// kobj.h -- the objects behind a process's handles (onyxrun.h): files, folders, streams (pipes, files as streams,
// the host's console), processes. A handle is a number >= 0x1000 in the process's table (given to the old calls as a
// pointer); an object may be in several tables (a pipe's two ends given to a child, the console).
//
// MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors.
//
#ifndef ONYXRUN_KOBJ_H
#define ONYXRUN_KOBJ_H

#include "onyxrun.h"
#include <stdio.h>
#include <filesystem>

enum ObjKind { H_FILE, H_DIR, H_PIPE, H_CONIN, H_CONOUT, H_FILEIN, H_FILEOUT, H_PROC, H_SHM, H_SOCK };

struct Pipe
{
	std::mutex m;
	std::condition_variable cv;
	std::deque<u8> data;
	bool eof = false;		// stream_eof, or the last writer gone
	int writers = 0;
};

struct Obj
{
	ObjKind kind;
	FILE *f = 0;				// H_FILE, H_FILEIN, H_FILEOUT
	unsigned oflags = 0;
	long long pos = 0;			// H_FILE's offset
	std::string onyx, host;
	std::vector<std::filesystem::directory_entry> entries;	// H_DIR
	size_t next = 0;
	std::shared_ptr<Pipe> pipe;		// H_PIPE
	Proc *proc = 0;				// H_PROC
	std::mutex m;
	~Obj ();
};

u64 h_add (Proc *P, std::shared_ptr<Obj> o);
std::shared_ptr<Obj> h_get (Proc *P, u64 h, int kind = -1);
bool h_drop (Proc *P, u64 h);
void h_drop_all (Proc *P);			// the process's end

std::shared_ptr<Obj> obj_console_in (void);	// the host's console (one object each)
std::shared_ptr<Obj> obj_console_out (void);
std::shared_ptr<Obj> obj_pipe (void);
// A stream's bytes: -> bytes, 0 the end, -1 an error; nb: -1 when it would block.
int obj_read (Obj &o, void *buf, unsigned n, bool nb);
// -> bytes, -1 an error; nb: -KAPI_EAGAIN when full.
int obj_write (Obj &o, const void *buf, unsigned n, bool nb);
void obj_eof (Obj &o);

#endif
