#pragma once

#include "Document.h"
#include "Language.h"
#include "Theme.h"

#include <QHash>
#include <QTextEdit>

#include <functional>

class QCompleter;
class QStringListModel;
class QMimeData;

/**
 * Editor de código: numeração de linhas, realce, indentação e pares automáticos.
 * Baseado em QTextEdit (layout rico) para suportar indentação das linhas
 * quebradas (wrap) — o QPlainTextEdit não a permite.
 */
class CodeEditor : public QTextEdit
{
    Q_OBJECT
public:
    explicit CodeEditor(Document *document, QWidget *parent = nullptr);

    Document *doc() const { return m_doc; }
    QString filePath() const { return m_doc->filePath(); }
    void setFilePath(const QString &path) { m_doc->setFilePath(path); }
    QString displayName() const { return m_doc->displayName(); }

    const Language &language() const { return m_doc->language(); }
    void setLanguage(const Language &language);
    void setTheme(const Theme &theme);
    void setEditorFont(const QFont &font);
    void setAutoClosePairs(bool enabled) { m_autoClosePairs = enabled; }
    void setTabWidthSpaces(int spaces);
    void setLineNumbersVisible(bool visible);
    void setWordWrap(bool wrap);
    /** Liga o recuo (suspenso) das linhas quebradas. Padrão desligado. */
    void setWrapIndentEnabled(bool on);
    /** Recuo extra (em espaços) nas linhas quebradas; 0 = modo VSCode (padrão). */
    void setWrapIndentExtra(int spaces);
    void setHighlightCurrentLine(bool enabled);
    void setCodeCompletion(bool enabled) { m_completionEnabled = enabled; }
    void setInsertSpaces(bool spaces) { m_insertSpaces = spaces; }
    /** Substituição de exibição de símbolos (texto intacto); vazio = desligado. */
    void setPrettySymbols(bool enabled, const QList<QPair<QString, QString>> &rules);

    /** Sugestão do servidor: rótulo exibido e texto inserido. */
    struct ServerItem { QString label; QString insert; };
    using CompletionProvider = std::function<void(int line, int column, std::function<void(QList<ServerItem>)>)>;
    /** Fonte assíncrona de sugestões (LSP) e caracteres que a disparam ("." "->"…). */
    void setCompletionProvider(CompletionProvider provider, const QStringList &triggers);

    /** Pede a definição do símbolo na posição (linha e coluna UTF-16, base 0). */
    using DefinitionProvider = std::function<void(int line, int column)>;
    void setDefinitionProvider(DefinitionProvider provider) { m_definitionProvider = std::move(provider); }
    bool hasDefinitionProvider() const { return bool(m_definitionProvider); }
    /** Dispara "ir para definição" na posição atual do cursor (ação/Ctrl+clique). */
    void goToDefinition();

    /** Documentação (hover) na posição; chama done com o texto (vazio = nada). */
    using HoverProvider = std::function<void(int line, int column, std::function<void(QString)> done)>;
    void setHoverProvider(HoverProvider provider) { m_hoverProvider = std::move(provider); }

    /** Posiciona o cursor em linha/coluna (base 0) e centraliza. */
    void goToLineColumn(int line, int column);
    bool insertSpaces() const { return m_insertSpaces; }
    int tabWidth() const { return m_indentSpaces; }

    bool loadFile(const QString &path, QString *error = nullptr);
    bool saveFile(const QString &path, QString *error = nullptr);

    void toggleComment();
    void goToLine(int line, bool focusEditor = true);
    /** Centraliza o cursor na vista (substitui QPlainTextEdit::centerCursor). */
    void centerCursor() { centerCursorCompat(); }

    /** Enter após esta linha (já sem espaços à direita) abre um novo nível de bloco? */
    static bool opensBlock(const QString &languageId, const QString &lineBeforeCursor);

    int lineNumberAreaWidth() const;
    void lineNumberAreaPaintEvent(QPaintEvent *event);
    void lineNumberAreaClicked(const QPoint &pos);
    void lineNumberAreaHovered(const QPoint &pos);

    void setMinimapVisible(bool visible);
    void setMinimapWidth(int width);
    int minimapWidth() const;
    void minimapPaintEvent(QPaintEvent *event);
    void minimapMousePressEvent(QMouseEvent *event);
    void minimapMouseMoveEvent(QMouseEvent *event);
    void minimapMouseReleaseEvent(QMouseEvent *event);
    void minimapWheelEvent(QWheelEvent *event);

    /** O modo desempenho (Document::isLargeFile) está em vigor neste editor? */
    bool performanceActive() const { return m_perfActive; }

Q_SIGNALS:
    void overwriteModeToggled(bool overwrite);
    /** Arquivos locais soltos sobre o editor: devem abrir em novas abas. */
    void filesDropped(const QStringList &paths);
    /** O modo desempenho entrou ou saiu de vigor (ex.: o arquivo cruzou o limite de linhas). */
    void performanceActiveChanged(bool active);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    bool viewportEvent(QEvent *event) override;
    // Arrastar/colar URLs de arquivos locais abre em novas abas em vez de inserir texto.
    bool canInsertFromMimeData(const QMimeData *source) const override;
    void insertFromMimeData(const QMimeData *source) override;

private:
    // Compatibilidade com a API de blocos do QPlainTextEdit (ausente no
    // QTextEdit): calculadas a partir do layout do documento e da rolagem.
    QTextBlock firstVisibleBlock() const;
    QRectF blockBoundingGeometry(const QTextBlock &block) const;
    QRectF blockBoundingRect(const QTextBlock &block) const;
    QPointF contentOffset() const;
    void centerCursorCompat();
    void onEditorViewportChanged(); //< substitui o sinal updateRequest
    // Indentação das linhas quebradas (wrap), como no VSCode: aplica um recuo
    // suspenso em cada bloco conforme a indentação da própria linha.
    void applyWrapIndent();
    // Modo desempenho: os recursos caros seguem a escolha do usuário (m_*Wanted)
    // só enquanto ele não estiver em vigor.
    void updatePerformanceState();
    void applyMinimapVisible();
    void applyWrapIndentEnabled();
    bool m_perfActive = false;
    bool m_minimapWanted = true;
    bool m_wrapIndentWanted = false;
    bool m_wordWrap = false;
    bool m_wrapIndentEnabled = false;  //< recuo das linhas quebradas (padrão desligado)
    int m_wrapIndentExtra = 0;         //< espaços extras no recuo do wrap (0 = VSCode)
    bool m_applyingWrapIndent = false;
    bool m_wrapIndentsApplied = false; //< há recuos de wrap aplicados aos blocos?
    int m_wrapIndentRevision = -1;
    int m_wrapDirtyFrom = -1; //< faixa editada pendente de recuo (posições)
    int m_wrapDirtyTo = -1;

    void updateLineNumberAreaWidth();
    void updateViewportMargins();
    void updateLineNumberArea(const QRect &rect, int dy);
    // Geometria do minimap num único modelo (igual ao VSCode): o indicador
    // (slider) é desenhado e arrastado a partir destes mesmos valores, para
    // que ele fique sempre sob o cursor durante o arraste.
    struct MinimapMetrics {
        int h = 0;               //< altura do minimap em px
        int lineCount = 1;       //< blocos do documento
        int visibleLines = 1;    //< linhas visíveis na viewport
        int sliderHeight = 0;    //< altura do indicador em px
        int maxSliderTop = 0;    //< curso do indicador (0..maxSliderTop)
        int sliderTop = 0;       //< topo atual do indicador em px
        int firstVisibleLine = 0;//< primeira linha visível (aprox.)
        int scrollRange = 0;     //< bar->maximum() - bar->minimum()
        int scrollMin = 0;       //< bar->minimum()
    };
    MinimapMetrics minimapMetrics() const;
    int minimapFirstLine() const;
    void minimapScrollToSliderTop(int sliderTop, const MinimapMetrics &m);
    void highlightCurrentLine() { updateExtraSelections(); }
    bool handleAutoPair(QKeyEvent *event);
    void insertNewlineWithIndent();
    void paintPrettySymbols(class QPainter &p);
    void paintIndentGuides(class QPainter &p);
    QString wordUnderCursor() const;
    int foldMarkerWidth() const;
    int breakpointWidth() const;
    void onFoldingChanged();
    void updateCompletion(bool fromTrigger = false);
    void showCompletions(const QString &prefix, QStringList words, const QList<ServerItem> &server);
    void updateExtraSelections();
    void appendSelectionOccurrences(QList<QTextEdit::ExtraSelection> &extra);
    const QString &plainTextCached() const;
    mutable QString m_plainText;
    mutable int m_plainRevision = -1;
    QString diagnosticTextAt(const QPoint &viewportPos) const;
    void insertCompletion(const QString &word);

    QWidget *m_lineNumberArea;
    QWidget *m_minimap = nullptr;
    bool m_minimapVisible = true;
    int m_minimapWidth = 55;         //< largura do minimap em px
    bool m_minimapDragging = false;
    int m_mmDragStartY = 0;          //< Y do mouse no início do arraste (relativo)
    int m_mmDragStartScroll = 0;     //< valor da rolagem no início do arraste
    QPixmap m_minimapCache;          //< fatia renderizada das linhas (sem o indicador)
    int m_mmCacheFirstLine = -1;     //< chave do cache: primeira linha desenhada
    int m_mmCacheRevision = -1;      //< …revisão do documento
    QString m_mmCacheThemeId;        //< …tema
    Document *m_doc;
    Theme m_theme;
    bool m_autoClosePairs = true;
    int m_indentSpaces = 4;
    bool m_lineNumbers = true;
    bool m_highlightLine = true;
    bool m_completionEnabled = true;
    bool m_insertSpaces = true;
    bool m_prettySymbols = false;
    QList<QPair<QString, QString>> m_symbols; //< (sequência, símbolo), mais longa primeiro
    int m_hoverBreakpointLine = -1;
    int m_reloadLine = 0;
    int m_reloadColumn = 0;
    int m_reloadScroll = 0;
    QString indentUnit() const;
    QCompleter *m_completer;
    QStringListModel *m_completionModel;
    CompletionProvider m_provider;
    DefinitionProvider m_definitionProvider;
    HoverProvider m_hoverProvider;
    QStringList m_triggers;
    QHash<QString, QString> m_insertFor; //< rótulo → texto a inserir
    int m_completionRequest = 0;
    QString m_ghostText;   //< sugestão inline (ghost text), aceita com Tab
    int m_ghostPos = -1;   //< posição do cursor onde o ghost é válido
    QString m_occNeedle;          //< seleção cujas ocorrências estão destacadas
    int m_occRevision = -1;
    QList<int> m_occPositions;
    // Faixas (início,fim) a contornar no paintEvent: pares de bloco/tag e
    // ocorrências. O preenchimento vem das ExtraSelections; o contorno é
    // pintado por cima para o destaque ficar nítido (estilo VS Code).
    QList<QPair<int, int>> m_matchRanges;
    QList<QPair<int, int>> m_occRanges;
    void paintHighlightOutlines(class QPainter &p);
    void setGhost(const QString &text);
    void clearGhost();
    void paintGhostText(class QPainter &p);
};
