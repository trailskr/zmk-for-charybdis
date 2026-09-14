/*
 * RuEn bindings, matching Ergohaven's RuEn mode.
 *
 * Use as: &ruen RUEN_DOT
 *     or: &ruen_dot
 */

#pragma once

/* Layout switch */
#define RUEN_TOGGLE 0
#define RUEN_SYNC 1
#define RUEN_EN 2
#define RUEN_RU 3
#define RUEN_M1M2 4
#define RUEN_DFLT 5

/* Symbols present in both layouts */
#define RUEN_DOT 6
#define RUEN_COMMA 7
#define RUEN_SEMI 8
#define RUEN_COLON 9
#define RUEN_DQT 10
#define RUEN_QMARK 11
#define RUEN_FSLH 12

#define RUEN_SCLN RUEN_SEMI
#define RUEN_DQUO RUEN_DQT
#define RUEN_QUES RUEN_QMARK
#define RUEN_SLASH RUEN_FSLH

/* English-only symbols (temporarily switch to EN if needed) */
#define RUEN_LBKT 13
#define RUEN_RBKT 14
#define RUEN_LBRC 15
#define RUEN_RBRC 16
#define RUEN_LT 17
#define RUEN_GT 18
#define RUEN_GRAVE 19
#define RUEN_TILDE 20
#define RUEN_AT 21
#define RUEN_HASH 22
#define RUEN_DLLR 23
#define RUEN_CARET 24
#define RUEN_AMPS 25
#define RUEN_PIPE 26
#define RUEN_SQT 27

#define RUEN_LBR RUEN_LBKT
#define RUEN_RBR RUEN_RBKT
#define RUEN_LCBR RUEN_LBRC
#define RUEN_RCBR RUEN_RBRC
#define RUEN_TILD RUEN_TILDE
#define RUEN_DLR RUEN_DLLR
#define RUEN_CIRC RUEN_CARET
#define RUEN_AMPR RUEN_AMPS
#define RUEN_QUOTE RUEN_SQT

#define RUEN_EN_FIRST RUEN_LBKT
#define RUEN_EN_LAST RUEN_SQT

/* Russian-only */
#define RUEN_NUM 28
#define RUEN_NUMERO RUEN_NUM

/* Extra */
#define RUEN_WORD 29
#define RUEN_MOD 30
#define RUEN_STORE 31
#define RUEN_REVERT 32
#define RUEN_PRCNT 33
#define RUEN_PERC RUEN_PRCNT
#define RUEN_TG_MAC 34

/* Russian letters that are punctuation on the English layout */
#define RUEN_BE 35
#define RUEN_YU 36
#define RUEN_ZHE 37
#define RUEN_E 38
#define RUEN_KHA 39
#define RUEN_HRD_SGN 40
#define RUEN_YO 41
#define RUEN_RUBLE 42

#define RUEN_HA RUEN_KHA
#define RUEN_HARD RUEN_HRD_SGN
#define RUEN_IO RUEN_YO
#define RUEN_RUB RUEN_RUBLE
