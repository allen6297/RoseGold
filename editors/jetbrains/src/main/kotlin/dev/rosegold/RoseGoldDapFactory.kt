package dev.rosegold

import com.intellij.execution.configurations.GeneralCommandLine
import com.intellij.execution.configurations.RunConfiguration
import com.intellij.execution.configurations.RunConfigurationOptions
import com.intellij.execution.process.ProcessHandler
import com.intellij.execution.runners.ExecutionEnvironment
import com.intellij.openapi.fileTypes.FileType
import com.intellij.openapi.project.Project
import com.intellij.openapi.vfs.VirtualFile
import com.redhat.devtools.lsp4ij.dap.DebugMode
import com.redhat.devtools.lsp4ij.dap.configurations.DAPRunConfigurationOptions
import com.redhat.devtools.lsp4ij.dap.descriptors.DebugAdapterDescriptor
import com.redhat.devtools.lsp4ij.dap.descriptors.DebugAdapterDescriptorFactory
import com.redhat.devtools.lsp4ij.dap.descriptors.DefaultDebugAdapterDescriptor

// Registers the native debug adapter (`rosegoldc --dap`) with LSP4IJ so JetBrains
// can debug .rg files — breakpoints, stepping, call stack, and variable inspection
// with drill-in — driven by the same DAP server the VS Code extension uses.
//
// One-click: LSP4IJ's built-in run-configuration producer offers a "Debug" action
// for any file this factory calls debuggable, and prepareConfiguration() (below)
// fills in the server, the file, and a "Debug <name>" title — so right-clicking a
// .rg file (or using the gutter) creates a ready-to-run RoseGold debug config.
class RoseGoldDapFactory : DebugAdapterDescriptorFactory() {
    override fun isDebuggableFile(file: VirtualFile, project: Project): Boolean =
        file.extension == "rg"

    override fun createDebugAdapterDescriptor(
        options: RunConfigurationOptions,
        environment: ExecutionEnvironment,
    ): DebugAdapterDescriptor = RoseGoldDapDescriptor(options, environment)

    // The base sets the server id, file, and launch mode from this factory's server
    // definition; we add a readable configuration name.
    override fun prepareConfiguration(configuration: RunConfiguration, file: VirtualFile, project: Project): Boolean {
        val ok = super.prepareConfiguration(configuration, file, project)
        configuration.name = "Debug ${file.name}"
        return ok
    }
}

class RoseGoldDapDescriptor(
    options: RunConfigurationOptions,
    environment: ExecutionEnvironment,
) : DefaultDebugAdapterDescriptor(options, environment, "RoseGold") {

    // Launch the native debug adapter; LSP4IJ speaks DAP over its stdio.
    override fun startServer(): ProcessHandler {
        val cmd = GeneralCommandLine(findRosegoldc(environment.project), "--dap")
        environment.project.basePath?.let { cmd.withWorkDirectory(it) }
        return startServer(cmd)
    }

    // The DAP `launch` request: point the VM at the .rg file chosen in the run config.
    override fun getDapParameters(): MutableMap<String, Any> {
        val program = (options as? DAPRunConfigurationOptions)?.file ?: ""
        return mutableMapOf("type" to "rosegold", "request" to "launch", "program" to program)
    }

    override fun getDebugMode(): DebugMode = DebugMode.LAUNCH
    override fun getFileType(): FileType = RoseGoldFileType
    override fun isDebuggableFile(file: VirtualFile, project: Project): Boolean = file.extension == "rg"
}
