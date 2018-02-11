#pragma once

#include <common/pony/detail/atomics.h>
#include <ponyc.h>

extern "C" {
#include <common/platform.h>
#include <libponyrt/mem/pool.h>
#include <common/paths.h>
#include <ast/parserapi.h>
#include <ast/treecheck.h>
#include <pkg/package.h>
#include <pass/pass.h>
#include <options/options.h>
}

