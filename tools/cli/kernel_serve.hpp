// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// `nf kernel serve`: RFC 0015's external kernel protocol. Other CAD programs send one JSON request
// per stdin line (operands as STEP text with a sample point per face and edge) and read one
// response per stdout line (the result as STEP text with every face's history in its STEP face
// order). stdout carries protocol lines only; anything a library prints goes to stderr.
#pragma once

namespace nf::cli {
/// Serves until stdin closes. argv[1] is "kernel", argv[2] "serve"; option --timeout SECONDS
/// (default 120) ends the process with status 3 when one request runs longer. Exit status 0 on
/// EOF, 2 on a usage error, 1 on a fatal error or an over-long line.
int run_kernel_serve(int argc, char** argv);
}  // namespace nf::cli
