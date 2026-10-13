#pragma once

// Runs all checks, prints what it did, returns 0 when everything passed.
// `recording` may be null; else the path of a .gt7r recording whose steering is used as well.
int runSelfTest(const char* recording);
