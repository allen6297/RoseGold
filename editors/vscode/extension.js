// RoseGold VS Code extension.
//
// Syntax highlighting + editor config come from the grammar and
// language-configuration.json (no code needed). This file starts the native
// RoseGold LANGUAGE SERVER -- `rosegoldc --lsp` -- and connects to it over
// stdio via the standard LSP client. That gives us, straight from the
// canonical C++ implementation:
//   • live diagnostics (parse / type errors)
//   • hover            (type of the identifier under the cursor)
//   • go-to-definition (top-level symbols and class members)
//   • completion       (members after `.` on a class or module)

const vscode = require("vscode");
const fs = require("fs");
const path = require("path");
const { LanguageClient, TransportKind } = require("vscode-languageclient/node");

let client;

// Locate the rosegoldc binary: explicit setting, then each workspace folder
// (checking cpp/rosegoldc and rosegoldc).
function findCompiler() {
  const cfg = vscode.workspace.getConfiguration("rosegold");
  const explicit = cfg.get("compilerPath");
  if (explicit && fs.existsSync(explicit)) return explicit;

  for (const f of vscode.workspace.workspaceFolders || []) {
    for (const rel of ["cpp/rosegoldc", "rosegoldc"]) {
      const cand = path.join(f.uri.fsPath, rel);
      if (fs.existsSync(cand)) return cand;
    }
  }
  return null;
}

function activate(context) {
  const compiler = findCompiler();
  if (!compiler) {
    vscode.window.showWarningMessage(
      "RoseGold: rosegoldc not found. Build it (clang++ -std=c++17 -O2 -o cpp/rosegoldc cpp/src/main.cpp) " +
      "or set 'rosegold.compilerPath'. Syntax highlighting still works."
    );
    return;
  }

  const serverOptions = {
    run:   { command: compiler, args: ["--lsp"], transport: TransportKind.stdio },
    debug: { command: compiler, args: ["--lsp"], transport: TransportKind.stdio },
  };
  const clientOptions = {
    documentSelector: [{ scheme: "file", language: "rosegold" }],
    synchronize: {
      fileEvents: vscode.workspace.createFileSystemWatcher("**/*.rg"),
    },
  };

  client = new LanguageClient(
    "rosegold",
    "RoseGold Language Server",
    serverOptions,
    clientOptions
  );
  client.start();
  context.subscriptions.push({ dispose: () => client && client.stop() });

  // Debugging: the same binary is a Debug Adapter (`rosegoldc --dap`).
  const factory = {
    createDebugAdapterDescriptor() {
      return new vscode.DebugAdapterExecutable(compiler, ["--dap"]);
    },
  };
  context.subscriptions.push(
    vscode.debug.registerDebugAdapterDescriptorFactory("rosegold", factory)
  );
  // F5 on a .rg file with no launch.json: debug the active file.
  const provider = {
    resolveDebugConfiguration(_folder, config) {
      if (!config.type && !config.request && !config.name) {
        const ed = vscode.window.activeTextEditor;
        if (ed && ed.document.languageId === "rosegold") {
          config.type = "rosegold";
          config.name = "Run RoseGold File";
          config.request = "launch";
          config.program = ed.document.fileName;
          config.stopOnEntry = false;
        }
      }
      if (!config.program) {
        return vscode.window
          .showInformationMessage("RoseGold: set 'program' to a .rg file to debug")
          .then(() => undefined);
      }
      return config;
    },
  };
  context.subscriptions.push(
    vscode.debug.registerDebugConfigurationProvider("rosegold", provider)
  );
}

function deactivate() {
  return client ? client.stop() : undefined;
}

module.exports = { activate, deactivate };
