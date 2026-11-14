// Lab by Veff (Veff fork): what a Lab commit plays and where its sample goes,
// for the shared capture coordinator (arranger_bounce.cpp).
#pragma once
#include <opxy/types.hpp>
extern "C" {
// Dispatch side: the commit's source (the pattern's bars, or the last note).
bool sdk_lab_source_begin() __asm__("sdk_lab_source_begin");
bool sdk_lab_source_start() __asm__("sdk_lab_source_start");
bool sdk_lab_source_running() __asm__("sdk_lab_source_running");
void sdk_lab_source_stop() __asm__("sdk_lab_source_stop");
void sdk_lab_source_end() __asm__("sdk_lab_source_end");
// GUI: the commit's file, and its assignment to T7 (and the palette).
const char* sdk_lab_path() __asm__("sdk_lab_path");
bool sdk_lab_destination_begin(const char* path, opxy::u32 frames) __asm__("sdk_lab_destination_begin");
opxy::u32 sdk_lab_destination_status() __asm__("sdk_lab_destination_status");
// The track whose regions stock sorts while Lab assigns a sample, or 8.
opxy::u32 sdk_lab_sort_track() __asm__("sdk_lab_sort_track");
}
