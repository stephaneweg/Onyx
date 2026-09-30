//
// stkpoc.cpp -- the SuperTuxKart port's first proof of concept (docs/SUPERTUXKART-PORT.md,
// milestone M0): a console program (a /bin-style newlib program, no window) that links two of
// SuperTuxKart's own libraries, built for Onyx from STK's tree (lib/bullet, lib/angelscript),
// against newlib + the full libstdc++ (exceptions, RTTI, the STL), as the game itself will need:
//
//   1. the C++ runtime: an exception thrown and caught across functions, dynamic_cast / typeid,
//      std::string / std::vector / std::map / std::unique_ptr, std::atomic; and the threads --
//      std::thread / std::mutex / std::recursive_mutex / std::condition_variable / std::call_once
//      on the kapi v67 threads (compat/bits/gthr-default.h);
//   2. Bullet 2.79 (STK's fork): a sphere dropped onto a static plane, 2 s at 60 Hz -- it must come
//      to rest at its radius above the plane;
//   3. AngelScript 2.35.1 (AS_MAX_PORTABILITY, the generic calling convention STK uses on AArch64):
//      a script compiled and run, calling back a native function.
//
// Prints each check and PASS / FAIL (exit code 0 / 1). Not staged: built only (user/stk/Makefile).
//
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <atomic>
#include <stdexcept>
#include <typeinfo>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>

#include "btBulletDynamicsCommon.h"
#include "angelscript.h"

static int s_fails;
static void check (bool ok, const char *what)
{
	printf ("  %-44s %s\n", what, ok ? "ok" : "FAILED");
	if (!ok)
		s_fails++;
}

// ---- 1. the C++ runtime -----------------------------------------------------------------------
struct Base { virtual ~Base () {} virtual int kind () const { return 0; } };
struct Kart : Base { int kind () const override { return 1; } };

static int parse_laps (const std::string &s)
{
	if (s.empty () || s[0] < '0' || s[0] > '9')
		throw std::runtime_error ("bad lap count: '" + s + "'");
	return atoi (s.c_str ());
}

static void test_runtime ()
{
	printf ("C++ runtime\n");
	bool caught = false;
	try { parse_laps ("x3"); }
	catch (const std::exception &e) { caught = strstr (e.what (), "bad lap count") != nullptr; }
	check (caught, "exception thrown and caught");

	std::unique_ptr<Base> b (new Kart);
	check (dynamic_cast<Kart *> (b.get ()) != nullptr, "dynamic_cast");
	check (typeid (*b) == typeid (Kart), "typeid");

	std::map<std::string, std::vector<int>> laps;
	for (int i = 0; i < 100; i++)
		laps["kart" + std::to_string (i % 4)].push_back (i);
	check (laps.size () == 4 && laps["kart3"].size () == 25, "std::map / std::vector / std::string");

	std::atomic<int> a (0);
	for (int i = 0; i < 1000; i++)
		a.fetch_add (1);
	check (a.load () == 1000, "std::atomic");
}

static void test_threads ()
{
	printf ("C++ threads (kapi v67)\n");
	std::mutex m;
	long counter = 0;
	std::vector<std::thread> workers;
	for (int t = 0; t < 4; t++)
		workers.emplace_back ([&] {
			for (int i = 0; i < 20000; i++)
			{
				std::lock_guard<std::mutex> g (m);
				counter++;
			}
		});
	for (auto &w : workers)
		w.join ();
	check (counter == 80000, "4 std::threads, std::mutex");

	// a producer / consumer queue: what STK's audio and loading threads do
	std::condition_variable cv;
	std::vector<int> queue;
	bool finished = false;
	long sum = 0;
	std::thread consumer ([&] {
		std::unique_lock<std::mutex> ul (m);
		for (;;)
		{
			cv.wait (ul, [&] { return !queue.empty () || finished; });
			while (!queue.empty ()) { sum += queue.back (); queue.pop_back (); }
			if (finished)
				break;
		}
	});
	for (int i = 1; i <= 100; i++)
	{
		{ std::lock_guard<std::mutex> g (m); queue.push_back (i); }
		cv.notify_one ();
		if (i % 10 == 0)
			std::this_thread::sleep_for (std::chrono::milliseconds (2));
	}
	{ std::lock_guard<std::mutex> g (m); finished = true; }
	cv.notify_all ();
	consumer.join ();
	check (sum == 5050, "std::condition_variable producer / consumer");

	std::recursive_mutex rm;
	{ std::lock_guard<std::recursive_mutex> a (rm); std::lock_guard<std::recursive_mutex> b (rm); }
	check (rm.try_lock (), "std::recursive_mutex");
	rm.unlock ();

	static std::once_flag once;
	int calls = 0;
	std::thread t1 ([&] { std::call_once (once, [&] { calls++; }); });
	std::call_once (once, [&] { calls++; });
	t1.join ();
	check (calls == 1, "std::call_once");
	check (std::this_thread::get_id () != std::thread::id (), "std::this_thread::get_id");
}

// ---- 2. Bullet --------------------------------------------------------------------------------
static void test_bullet ()
{
	printf ("Bullet %d\n", btGetVersion ());
	btDefaultCollisionConfiguration conf;
	btCollisionDispatcher dispatcher (&conf);
	btDbvtBroadphase broadphase;
	btSequentialImpulseConstraintSolver solver;
	btDiscreteDynamicsWorld world (&dispatcher, &broadphase, &solver, &conf);
	world.setGravity (btVector3 (0, -9.81f, 0));

	btStaticPlaneShape groundShape (btVector3 (0, 1, 0), 0);
	btDefaultMotionState groundMotion;
	btRigidBody ground (btRigidBody::btRigidBodyConstructionInfo (0, &groundMotion, &groundShape));
	world.addRigidBody (&ground);

	const btScalar radius = 0.5f;
	btSphereShape ballShape (radius);
	btVector3 inertia (0, 0, 0);
	ballShape.calculateLocalInertia (1, inertia);
	btDefaultMotionState ballMotion (btTransform (btQuaternion::getIdentity (), btVector3 (0, 10, 0)));
	btRigidBody ball (btRigidBody::btRigidBodyConstructionInfo (1, &ballMotion, &ballShape, inertia));
	world.addRigidBody (&ball);

	for (int i = 0; i < 180; i++)
		world.stepSimulation (1.0f / 60, 4);
	btTransform t;
	ballMotion.getWorldTransform (t);
	float y = t.getOrigin ().getY ();
	printf ("  the ball at y = %.3f after 3 s\n", y);
	check (fabsf (y - radius) < 0.05f, "sphere at rest on the plane");

	world.removeRigidBody (&ball);
	world.removeRigidBody (&ground);
}

// ---- 3. AngelScript ---------------------------------------------------------------------------
static int s_reported = -1;
static void as_report (asIScriptGeneric *gen)	// void report (int), the generic convention
{
	s_reported = (int) gen->GetArgDWord (0);
}
static void as_message (const asSMessageInfo *msg, void *)
{
	printf ("  as: %s (%d, %d): %s\n", msg->section, msg->row, msg->col, msg->message);
}

static void test_angelscript ()
{
	printf ("AngelScript %s (%s)\n", asGetLibraryVersion (), asGetLibraryOptions ());
	asIScriptEngine *engine = asCreateScriptEngine ();
	check (engine != nullptr, "engine created");
	if (!engine)
		return;
	engine->SetMessageCallback (asFUNCTION (as_message), nullptr, asCALL_CDECL);
	int r = engine->RegisterGlobalFunction ("void report (int)", asFUNCTION (as_report), asCALL_GENERIC);
	check (r >= 0, "native function registered");

	static const char script[] =
		"int fib (int n) { return n < 2 ? n : fib (n - 1) + fib (n - 2); }\n"
		"int main () { int s = 0; for (int i = 0; i < 10; i++) s += fib (i);\n"
		"  report (s); return fib (20); }\n";
	asIScriptModule *mod = engine->GetModule ("poc", asGM_ALWAYS_CREATE);
	mod->AddScriptSection ("poc.as", script);
	r = mod->Build ();
	check (r >= 0, "script compiled");
	asIScriptFunction *fn = mod->GetFunctionByDecl ("int main ()");
	asIScriptContext *ctx = engine->CreateContext ();
	int ret = -1;
	if (fn && ctx && ctx->Prepare (fn) >= 0 && ctx->Execute () == asEXECUTION_FINISHED)
		ret = (int) ctx->GetReturnDWord ();
	printf ("  main () = %d, report (%d)\n", ret, s_reported);
	check (ret == 6765 && s_reported == 88, "script run, native call-back");
	if (ctx)
		ctx->Release ();
	engine->ShutDownAndRelease ();
}

int main ()
{
	printf ("stkpoc -- SuperTuxKart port, milestone M0\n");
	test_runtime ();
	test_threads ();
	test_bullet ();
	test_angelscript ();
	printf (s_fails ? "FAIL (%d)\n" : "PASS\n", s_fails);
	return s_fails ? 1 : 0;
}
