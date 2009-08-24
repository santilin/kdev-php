/*
 * KDevelop Php Code Completion Support
 *
 * Copyright 2006-2008 Hamish Rodda <rodda@kde.org>
 * Copyright 2007-2008 David Nolden <david.nolden.kdevelop@art-master.de>
 * Copyright 2008 Niko Sams <niko.sams@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Library General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include "model.h"

#include <QIcon>
#include <QMetaType>
#include <QTextFormat>
#include <QBrush>
#include <QDir>
#include <kdebug.h>
#include <ktexteditor/view.h>
#include <ktexteditor/document.h>
#include <kiconloader.h>
#include <khtmlview.h>


#include <language/duchain/declaration.h>
#include <language/duchain/ducontext.h>
#include <language/duchain/duchain.h>
#include <language/duchain/parsingenvironment.h>
#include <editorintegrator.h>
#include <language/duchain/duchainlock.h>
#include <language/duchain/duchainbase.h>
#include <language/duchain/topducontext.h>
#include <language/duchain/duchainutils.h>
#include <language/duchain/dumpchain.h>

#include "duchain/classmethoddeclaration.h"

#include "context.h"
#include "worker.h"
#include "phplanguagesupport.h"

using namespace KTextEditor;
using namespace KDevelop;

namespace Php
{

CodeCompletionModel::CodeCompletionModel(QObject * parent)
        : KDevelop::CodeCompletionModel(parent)
{
}

KDevelop::CodeCompletionWorker* CodeCompletionModel::createCompletionWorker()
{
    return new CodeCompletionWorker(this);
}

CodeCompletionModel::~CodeCompletionModel()
{
}

Range CodeCompletionModel::completionRange(View* view, const Cursor &position)
{
    Range range = KDevelop::CodeCompletionModel::completionRange(view, position);
    if (range.start().column() > 0) {
        KTextEditor::Range preRange(Cursor(range.start().line(), range.start().column() - 1),
                                    Cursor(range.start().line(), range.start().column()));
        kDebug() << preRange << view->document()->text(preRange);
        const QString contents = view->document()->text(preRange);
        if ( contents == "$" ) {
            range.expandToRange(preRange);
            kDebug() << "using custom completion range" << range;
        }
    }
    return range;
}

bool CodeCompletionModel::shouldAbortCompletion(View* view, const SmartRange &range, const QString &currentCompletion)
{
    Q_UNUSED(view);
    Q_UNUSED(range);
    static const QRegExp allowedText("^\\$?(\\w*)");
    return !allowedText.exactMatch(currentCompletion);
}

}

#include "model.moc"
