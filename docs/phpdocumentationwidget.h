/*  This file is part of the KDevelop PHP Documentation Plugin

    Copyright 2012 Milian Wolff <mail@milianw.de>

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation; either version 2 of
    the License or (at your option) version 3 or any later version
    accepted by the membership of KDE e.V. (or its successor approved
    by the membership of KDE e.V.), which shall act as a proxy
    defined in Section 14 of version 3 of the license.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef PHPDOCUMENTATIONWIDGET_H
#define PHPDOCUMENTATIONWIDGET_H

#include <QStackedWidget>

class QTemporaryFile;
class QUrl;

namespace KDevelop {
    class StandardDocumentationView;
    class DocumentationFindWidget;
}

class PhpDocsPlugin;

class PhpDocumentationWidget : public QStackedWidget
{
    Q_OBJECT

public:
    explicit PhpDocumentationWidget(KDevelop::DocumentationFindWidget* find, const QUrl &url,
                                    PhpDocsPlugin* provider, QWidget* parent = nullptr);
    ~PhpDocumentationWidget() override;

private slots:
    /// used to inject some custom CSS to alter the remote php.net site
    void documentLoaded();
    void linkClicked(const QUrl& url);

private:
    KDevelop::StandardDocumentationView* m_part;
    QWidget* m_loading;
    QTemporaryFile* m_styleSheet;
    PhpDocsPlugin* m_provider;
};

#endif // PHPDOCUMENTATIONWIDGET_H
