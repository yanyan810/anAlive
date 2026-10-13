#include "HotelStageChecks.h"
int main() {
    try { HotelStageChecks::Validate("../../resources"); }
    catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
