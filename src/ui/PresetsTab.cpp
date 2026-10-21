#include "PresetsTab.h"
#include "MainWindow.h"
#include "../core/PresetManager.h"
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace vc {

PresetsTab::PresetsTab(MainWindow* win, QWidget* parent)
    : QWidget(parent), m_win(win)
{
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(18, 18, 18, 18);
    root->setSpacing(16);

    m_list = new QListWidget;
    m_list->setMinimumWidth(280);
    root->addWidget(m_list, 1);

    auto* right = new QVBoxLayout;
    m_desc = new QLabel;
    m_desc->setWordWrap(true);
    m_desc->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_desc->setMinimumHeight(120);
    right->addWidget(m_desc, 1);

    m_apply = new QPushButton(QStringLiteral("Apply to engine"));
    m_save = new QPushButton(QStringLiteral("Save current settings as new preset…"));
    m_del = new QPushButton(QStringLiteral("Delete user preset"));
    right->addWidget(m_apply);
    right->addWidget(m_save);
    right->addWidget(m_del);

    auto* note = new QLabel(QStringLiteral(
        "Built-in presets cannot be overwritten. Custom presets are stored "
        "as JSON files in your config directory and are plain, portable text."));
    note->setObjectName(QStringLiteral("muted"));
    note->setWordWrap(true);
    right->addWidget(note);
    right->addStretch(1);
    root->addLayout(right, 2);

    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0) return;
        const QString name = m_list->item(row)->text();
        if (const Preset* p = m_win->presets().find(name))
            m_desc->setText(QStringLiteral("<b>%1</b><br><br>%2").arg(p->name.toHtmlEscaped(),
                                                                    p->description.toHtmlEscaped()));
    });
    connect(m_apply, &QPushButton::clicked, this, [this]() {
        const int row = m_list->currentRow();
        if (row < 0) return;
        m_win->applyPresetByName(m_list->item(row)->text());
    });
    connect(m_save, &QPushButton::clicked, this, [this]() {
        QString name = QInputDialog::getText(this,
            QStringLiteral("Save preset"),
            QStringLiteral("Preset name:"));
        if (name.trimmed().isEmpty())
            return;
        Preset p;
        p.name = name.trimmed();
        p.description = QStringLiteral("User preset created from current settings.");
        p.params = m_win->currentParams();
        QString err;
        if (!m_win->presets().saveUserPreset(p, &err))
            QMessageBox::warning(this, QStringLiteral("Save failed"), err);
        else
            refreshList();
    });
    connect(m_del, &QPushButton::clicked, this, [this]() {
        const int row = m_list->currentRow();
        if (row < 0) return;
        const QString name = m_list->item(row)->text();
        QString err;
        if (!m_win->presets().deleteUserPreset(name, &err))
            QMessageBox::warning(this, QStringLiteral("Delete failed"), err);
        else
            refreshList();
    });
    refreshList();
}

void PresetsTab::refreshList()
{
    m_list->clear();
    for (const auto& p : m_win->presets().all())
        m_list->addItem(p.name);
    selectPreset(m_win->settings().get().lastPreset);
}

void PresetsTab::selectPreset(const QString& name)
{
    for (int i = 0; i < m_list->count(); ++i) {
        if (m_list->item(i)->text() == name) {
            m_list->setCurrentRow(i);
            return;
        }
    }
    if (m_list->count() > 0)
        m_list->setCurrentRow(0);
}

} // namespace vc
