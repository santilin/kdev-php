/***************************************************************************
 *   This file is part of KDevelop                                         *
 *   Copyright 2008 Niko Sams <niko.sams@gmail.com>                        *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Library General Public License as       *
 *   published by the Free Software Foundation; either version 2 of the    *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this program; if not, write to the                 *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.         *
 ***************************************************************************/
#ifndef PHPDUCHAINHELPER_H
#define PHPDUCHAINHELPER_H

#include "phpduchainexport.h"

#include <language/duchain/identifier.h>
#include <language/duchain/ducontext.h>
#include <language/duchain/indexedstring.h>

namespace KDevelop {
    class Declaration;
    class DUContext;
}
namespace Php {
    class AstNode;
    class CommonScalarAst;
    class EditorIntegrator;

    enum DeclarationType {
        ClassDeclarationType,
        FunctionDeclarationType,
        ConstantDeclarationType,
        GlobalVariableDeclarationType
    };
    KDevelop::Declaration* findDeclarationImportHelper(KDevelop::DUContext* currentContext,
                                        KDevelop::QualifiedIdentifier id,
                                        DeclarationType declarationType,
                                        AstNode* node,
                                        EditorIntegrator* editor,
                                        bool createProblems);

    QString formatComment(AstNode* node, EditorIntegrator* editor);


    CommonScalarAst* findCommonScalar(AstNode* node);

    KDevelop::IndexedString findIncludeFileUrl(const QString &includeFile, const KUrl &currentUrl);
}
#endif
