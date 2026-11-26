#include "board.h"

const char *const STATUS_NAME[STATUS_COUNT] = {"/ACK", "BUSY", "PE", "SELECT", "/ERROR"};
const uint8_t STATUS_DB25[STATUS_COUNT] = {10, 11, 12, 13, 15};
const char *const DATA_NAME[DATA_COUNT] = {"D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7"};
const uint8_t DATA_DB25[DATA_COUNT] = {2, 3, 4, 5, 6, 7, 8, 9};
const char *const CTRL_NAME[CTRL_COUNT] = {"/STROBE", "/AUTOFD", "/INIT", "/SELIN"};
const uint8_t CTRL_DB25[CTRL_COUNT] = {1, 14, 16, 17};
