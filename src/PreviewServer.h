#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QUrl>

#include <functional>

class QTcpServer;
class QTcpSocket;
class QTimer;

/**
 * Servidor HTTP estático embutido para a pré-visualização de HTML/CSS/JS.
 * Serve os arquivos de um diretório e, quando o live-reload está ligado,
 * injeta um pequeno script que recarrega a página no navegador do sistema
 * quando o conteúdo muda (ou em intervalos). Assim o "render em tempo real"
 * funciona em qualquer navegador, sem depender de ferramentas externas.
 */
class PreviewServer : public QObject
{
    Q_OBJECT
public:
    enum ReloadMode { Never, OnChange, Interval };

    explicit PreviewServer(QObject *parent = nullptr);
    ~PreviewServer() override;

    /** Serve rootDir em host:port (port 0 = porta livre). false se falhar. */
    bool start(const QString &rootDir, const QString &host, quint16 port);
    void stop();
    bool isRunning() const;

    QString rootDir() const { return m_root; }
    QString host() const { return m_host; }
    quint16 port() const { return m_port; }

    /** Modo de recarga e intervalo (ms) usado no modo Interval. */
    void setReload(ReloadMode mode, int intervalMs);
    /** Marca que o conteúdo mudou (modo OnChange): recarrega na próxima sondagem. */
    void triggerReload();

    /** URL http:// para um arquivo dentro do rootDir (vazia se fora dele). */
    QUrl urlForFile(const QString &filePath) const;

    /**
     * Fonte de conteúdo ao vivo: dado o caminho absoluto, preenche "out" com o
     * texto atual do editor (se o arquivo estiver aberto e modificado) e
     * devolve true; caso contrário, false (serve do disco). Permite recarregar
     * mostrando edições não salvas.
     */
    using ContentProvider = std::function<bool(const QString &absPath, QByteArray &out)>;
    void setContentProvider(ContentProvider provider) { m_provider = std::move(provider); }

private:
    void onNewConnection();
    void serve(QTcpSocket *sock, const QByteArray &requestLine);
    QByteArray injectReload(const QByteArray &html) const;

    QTcpServer *m_server = nullptr;
    QString m_root;
    QString m_host;
    quint16 m_port = 0;
    ReloadMode m_mode = Never;
    int m_intervalMs = 1000;
    quint64 m_token = 1;
    QTimer *m_intervalTimer = nullptr;
    QHash<QTcpSocket *, QByteArray> m_buffers;
    ContentProvider m_provider;
};
