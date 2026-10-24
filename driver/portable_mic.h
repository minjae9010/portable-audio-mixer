#pragma once
#include <ntddk.h>
#include "../shared/pcm_ring.hpp"
NTSTATUS PortableMicInitialize(PDRIVER_OBJECT driver);
void PortableMicShutdown();
void PortableMicRead(pm::Cursor& cursor, PUCHAR destination, ULONG bytes);
