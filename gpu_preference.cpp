#ifdef _WIN32
// Declare application preference without modifying the user's Windows settings.
// Windows per-application graphics preferences still have final priority.
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif
