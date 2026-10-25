// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// `nf mcp`: a stdio Model Context Protocol server (JSON-RPC 2.0, one message per line) over one
// Session (RFC 0011, Track 1). stdout carries protocol messages only; stderr carries diagnostics.
#pragma once

namespace nf::cli {
/// Runs the server until stdin closes. argv[1] is "mcp"; options: --in part.nightfall,
/// --allow-path DIR (repeatable). Exit status 0 on EOF, 2 on a usage error, 1 on a fatal error.
int run_mcp(int argc, char** argv);
}  // namespace nf::cli
