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
#include "helper.h"

#include <KIO/NetAccess>
#include <KParts/MainWindow>
#include <KLocalizedString>

#include <language/duchain/ducontext.h>
#include <language/duchain/duchainlock.h>
#include <language/duchain/persistentsymboltable.h>
#include <language/duchain/duchain.h>
#include <language/duchain/functiondeclaration.h>
#include <language/duchain/stringhelpers.h>
#include <language/duchain/parsingenvironment.h>
#include <interfaces/icore.h>
#include <interfaces/iprojectcontroller.h>
#include <interfaces/iuicontroller.h>
#include <interfaces/iproject.h>
#include <project/projectmodel.h>

#include "editorintegrator.h"
#include "../parser/parsesession.h"
#include "phpast.h"
#include "phpdefaultvisitor.h"
#include "constantdeclaration.h"

#define ifDebug(x) x

using namespace KDevelop;

namespace Php {

bool isMatch(Declaration* declaration, DeclarationType declarationType)
{
    if (declarationType == ClassDeclarationType && declaration->internalContext()
        && declaration->internalContext()->type() == DUContext::Class
    ) {
        return true;
    } else if(declarationType == FunctionDeclarationType
        && dynamic_cast<FunctionDeclaration*>(declaration)
    ) {
        return true;
    } else if(declarationType == ConstantDeclarationType
        && dynamic_cast<ConstantDeclaration*>(declaration)
    ) {
        return true;
    } else if(declarationType == GlobalVariableDeclarationType
        && declaration->kind() == Declaration::Instance
        && !dynamic_cast<ConstantDeclaration*>(declaration)
    ) {
        return true;
    }
    return false;
}

Declaration* findDeclarationImportHelper(DUContext* currentContext, QualifiedIdentifier id,
                                    DeclarationType declarationType, AstNode* node, EditorIntegrator* editor, bool createProblems)
{
    ifDebug( kDebug() << id.toString() << declarationType; )
    if (declarationType == ClassDeclarationType && id == QualifiedIdentifier("self")) {
        DUChainReadLocker lock(DUChain::lock());
        if (currentContext->parentContext()) {
            Declaration* declaration = currentContext->parentContext()->owner();
            return declaration;
        }
    } else if (declarationType == ClassDeclarationType && id == QualifiedIdentifier("parent")) {
        //there can be just one Class-Context imported
        DUChainReadLocker lock(DUChain::lock());
        if (currentContext->parentContext()) {
            foreach (DUContext::Import i, currentContext->parentContext()->importedParentContexts()) {
                if (i.context(currentContext->topContext())->type() == DUContext::Class) {
                    return i.context(currentContext->topContext())->owner();
                }
            }
        }
    } else {
        QList<Declaration*> foundDeclarations;
        DUContext *searchContext = currentContext;
        if (declarationType == GlobalVariableDeclarationType) {
            searchContext = currentContext->topContext();
        }
        DUChainReadLocker lock(DUChain::lock());
        foundDeclarations = currentContext->findDeclarations(id);
        foreach (Declaration *declaration, foundDeclarations) {
            if (isMatch(declaration, declarationType)) {
                ifDebug( kDebug() << "found matching declaration" << declaration->toString(); )
                lock.unlock();
                DUChainWriteLocker wlock(DUChain::lock());
                //currentContext->topContext()->updateImportsCache();
                if (!currentContext->topContext()->imports(declaration->context()->topContext(), SimpleCursor())) {
                    currentContext->topContext()->addImportedParentContext(declaration->context()->topContext());
                }
                return declaration;
            }
        }
        if (declarationType != GlobalVariableDeclarationType) {
            ifDebug( kDebug() << "No declarations found with findDeclarations, trying through PersistentSymbolTable" << id.toString(); )
            uint nr;
            const IndexedDeclaration* declarations = 0;
            PersistentSymbolTable::self().declarations(id, nr, declarations);
            ifDebug( kDebug() << "found declarations:" << nr; )
            lock.unlock();

            DUChainWriteLocker wlock(DUChain::lock());
            for (uint i=0; i<nr; ++i) {
                if (!declarations[i].declaration()) {
                    ifDebug( kDebug() << "skipping declaration, doesn't have declaration"; )
                    continue;
                } else if (!isMatch(declarations[i].declaration(), declarationType)) {
                    ifDebug( kDebug() << "skipping declaration, doesn't match with declarationType"; )
                    continue;
                }
                TopDUContext* top = declarations[i].declaration()->context()->topContext();
                if (top->language() != IndexedString("Php")) {
                    ifDebug( kDebug() << "skipping declaration, invalid language" << top->language().str(); )
                    continue;
                }
                if (ICore::self()) {
                    bool loadedProjectContainsUrl = false;
                    foreach (IProject *project, ICore::self()->projectController()->projects()) {
                        if (project->fileSet().contains(top->url())) {
                            loadedProjectContainsUrl = true;
                            break;
                        }
                    }
                    if (!loadedProjectContainsUrl) {
                        ifDebug( kDebug() << "skipping declaration, not in loaded project"; )
                        continue;
                    }
                }
                currentContext->topContext()->addImportedParentContext(top);
                currentContext->topContext()->parsingEnvironmentFile()
                    ->addModificationRevisions(top->parsingEnvironmentFile()->allModificationRevisions());
                ifDebug( kDebug() << "using" << declarations[i].declaration()->toString() << top->url().str(); )
                return declarations[i].declaration();
            }
        }
    }

    if (createProblems) {
        KDevelop::Problem *p = new KDevelop::Problem();
        p->setSource(KDevelop::ProblemData::DUChainBuilder);
        QString declarationTypeString;
        switch (declarationType) {
            case ClassDeclarationType:
                declarationTypeString = "class";
                break;
            case FunctionDeclarationType:
                declarationTypeString = "function";
                break;
            case ConstantDeclarationType:
                declarationTypeString = "constant";
                break;
            case GlobalVariableDeclarationType:
                declarationTypeString = "global";
                break;
        }
        p->setDescription(i18n("Could not find %1 '%2'", declarationTypeString, id.toString()));
        p->setFinalLocation(KDevelop::DocumentRange(editor->currentUrl().str(), editor->findRange(node).textRange()));
        {
            DUChainWriteLocker lock(DUChain::lock());
            ifDebug( kDebug() << "Problem" << p->description(); )
            currentContext->topContext()->addProblem(KDevelop::ProblemPointer(p));
        }
    }
    return 0;
}

QString formatComment(AstNode* node, EditorIntegrator* editor)
{
    return KDevelop::formatComment(editor->parseSession()->docComment(node->startToken));
}

//Helper visitor to extract a commonScalar node
//used to get the value of an function call argument
class ScalarExpressionVisitor : public DefaultVisitor
{
public:
    ScalarExpressionVisitor() : m_node(0) {}
    CommonScalarAst* node() const { return m_node; }
private:
    virtual void visitCommonScalar(CommonScalarAst* node) {
        m_node = node;
    }
    CommonScalarAst* m_node;
};

CommonScalarAst* findCommonScalar(AstNode* node)
{
    ScalarExpressionVisitor visitor;
    visitor.visitNode(node);
    return visitor.node();
}


bool includeExists(const IndexedString& url)
{
    {
        DUChainReadLocker lock(DUChain::lock());
        if (DUChain::self()->chainForDocument(url)) {
            return true;
        }
    }
    QWidget *w = 0;
    if (ICore::self() && ICore::self()->uiController()) w = ICore::self()->uiController()->activeMainWindow();
    if (KIO::NetAccess::exists(url.toUrl(), KIO::NetAccess::DestinationSide, w)) {
        return true;
    }
    return false;
}

IndexedString findIncludeFileUrl(const QString &includeFile, const KUrl &currentUrl)
{
    //look for file relative to current file
    QString currentDir = currentUrl.path();
    currentDir = currentDir.left(currentDir.lastIndexOf('/')+1);
    IndexedString url(currentDir+includeFile);
    if (includeExists(url)) return url;

    //first look in own project
    IProject* ownProject = ICore::self()->projectController()->findProjectForUrl(currentUrl);
    if (ownProject) {
        KUrl u = ownProject->projectItem()->url();
        u.addPath(includeFile);
        url = IndexedString(u.path());
        if (includeExists(url)) return url;
    }

    //then in all open projects
    foreach (IProject* project, ICore::self()->projectController()->projects()) {
        if (project == ownProject) continue;
        KUrl u = project->projectItem()->url();
        u.addPath(includeFile);
        url = IndexedString(u.path());
        if (includeExists(url)) return url;
    }

    //TODO configurable include paths

    return IndexedString();
}

}
