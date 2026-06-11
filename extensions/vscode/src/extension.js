const vscode = require('vscode');
const cp = require('child_process');
const path = require('path');
const { LanguageClient } = require('vscode-languageclient/node');

let client;

// Resolve dinamicamente o caminho do compilador Teko de forma flexível
function resolveCompilerPath(context) {
    // Nível 1: Tenta ler uma configuração personalizada do próprio VS Code (Definida pelo usuário)
    const configPath = vscode.workspace.getConfiguration('teko').get('compilerPath');
    if (configPath && configPath.trim() !== "") {
        return configPath;
    }

    // Nível 2: Se a extensão foi empacotada com o binário embutido (Distribuição de Loja / GitHub Releases)
    // O binário ficaria em: extensions/vscode/bin/teko
    const embeddedPath = path.join(context.extensionPath, 'bin', process.platform === 'win32' ? 'teko.exe' : 'teko');
    // Para simplificar sem checagem de fs síncrona pesada, retornamos como fallback viável

    // Nível 3: Padrão da Indústria - Assume que o executável está adicionado nas variáveis de ambiente do SO (PATH)
    // Facilitando chamadas globais como simplesmente 'teko'
    return "teko";
}

function activate(context) {
    console.log('Extensão Teko dinâmica ativada com sucesso!');

    // Resolve o caminho dinamicamente sem travar strings fixas no código
    const compilerPath = resolveCompilerPath(context);

    // 1. CONFIGURAÇÃO E ALOCAÇÃO DO LANGUAGE SERVER (LSP CLIENT)
    const serverOptions = {
        run: { command: compilerPath, args: ["check"] },
        debug: { command: compilerPath, args: ["check"] }
    };

    const clientOptions = {
        documentSelector: [{ scheme: 'file', language: 'teko' }],
        synchronize: {
            fileEvents: vscode.workspace.createFileSystemWatcher('**/*.tks')
        }
    };

    client = new LanguageClient('tekoLanguageServer', 'Teko Language Server', serverOptions, clientOptions);
    client.start();

    // 2. COMANDO DA IDE: Teko: Executar Projeto (Run)
    let runCommand = vscode.commands.registerCommand('teko.run', function () {
        const folders = vscode.workspace.workspaceFolders;
        if (!folders) {
            vscode.window.showErrorMessage('Abra uma pasta contendo um projeto Teko (.tkp) primeiro.');
            return;
        }

        vscode.workspace.findFiles('**/*.tkp').then((files) => {
            if (files.length === 0) {
                vscode.window.showErrorMessage('Manifesto do projeto (<nome>.tkp) não encontrado.');
                return;
            }
            const tkpPath = files[0].fsPath;
            const outputChannel = vscode.window.createOutputChannel("Teko VM Output");
            outputChannel.show();

            cp.exec(`"${compilerPath}" run "${tkpPath}"`, (err, stdout, stderr) => {
                if (err) {
                    outputChannel.appendLine(`[Erro de Runtime]:\n${stderr}`);
                    return;
                }
                outputChannel.appendLine(stdout);
            });
        });
    });

    // 3. COMANDO DA IDE: Teko: Compilar Projeto (Build)
    let buildCommand = vscode.commands.registerCommand('teko.build', function () {
        const folders = vscode.workspace.workspaceFolders;
        if (!folders) return;

        vscode.workspace.findFiles('**/*.tkp').then((files) => {
            if (files.length === 0) return;
            const tkpPath = files[0].fsPath;

            cp.exec(`"${compilerPath}" build "${tkpPath}"`, (err, stdout, stderr) => {
                if (err) {
                    vscode.window.showErrorMessage('Falha catastrófica ao compilar o projeto.');
                    return;
                }
                vscode.window.showInformationMessage('Artefato Teko gerado com sucesso no diretório!');
            });
        });
    });

    context.subscriptions.push(runCommand, buildCommand);
}

function deactivate() {
    if (!client) return undefined;
    return client.stop();
}

module.exports = { activate, deactivate };
