// Test-only entry: exercise unchanged production main with an isolated lock.
// Refuse every invocation that could construct a hardware-backed client.
#define main windmiProductionMain
#include "../../src/main.cpp"
#undef main

int main(int argc, char** argv)
{
  bool demo = false;
  for (int i = 1; i < argc; ++i)
  {
    if (strcmp(argv[i], "--demo") == 0)
      demo = true;
  }
  const char* lock = std::getenv("WINDMI_TEST_INSTANCE_LOCK");
  if (!demo || !lock || !*lock)
    return 97;
  windmi::platform::set_instance_lock_name_for_test(lock);
  return windmiProductionMain(argc, argv);
}
