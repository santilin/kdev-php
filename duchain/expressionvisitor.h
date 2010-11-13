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
#ifndef EXPRESSIONVISITOR_H
#define EXPRESSIONVISITOR_H

#include "phpdefaultvisitor.h"
#include "phpduchainexport.h"
#include "expressionevaluationresult.h"
#include "helper.h"

#include <language/duchain/types/abstracttype.h>
#include <language/duchain/identifier.h>

namespace KDevelop
{
class TopDUContext;
class Declaration;
}

namespace Php
{
class EditorIntegrator;

class KDEVPHPDUCHAIN_EXPORT ExpressionVisitor : public DefaultVisitor
{
public:
    ExpressionVisitor(EditorIntegrator* editor);
    ExpressionEvaluationResult result() {
        return m_result;
    }
    void setCreateProblems(bool v);
    void setOffset(const KDevelop::CursorInRevision& offset);

    virtual void visitNode(AstNode *node);

protected:
    KDevelop::DeclarationPointer processVariable( VariableIdentifierAst* variable);

    void visitAssignmentExpression(AssignmentExpressionAst *node);
    virtual void visitBaseVariable(BaseVariableAst* node);
    void visitCompoundVariableWithSimpleIndirectReference(CompoundVariableWithSimpleIndirectReferenceAst *node);
    void visitVarExpressionNewObject(VarExpressionNewObjectAst *node);
    void visitVarExpressionNormal(VarExpressionNormalAst *node);
    virtual void visitClosure(ClosureAst* node);
    void visitFunctionCall(FunctionCallAst* node);
    void visitConstantOrClassConst(ConstantOrClassConstAst *node);
    void visitScalar(ScalarAst *node);
    void visitStaticScalar(StaticScalarAst *node);
    void visitEncapsVar(EncapsVarAst *node);
    void visitVariableProperty(VariablePropertyAst *node);
    void visitStaticMember(StaticMemberAst* node);
    void visitUnaryExpression(UnaryExpressionAst* node);
    void visitAdditiveExpressionRest(AdditiveExpressionRestAst* node);
    void visitVariable(VariableAst* node);
    virtual void visitFunctionCallParameterList( FunctionCallParameterListAst* node );

    QString stringForNode(AstNode* id);
    KDevelop::QualifiedIdentifier identifierForNode(IdentifierAst* id);
    QString stringForNode(VariableIdentifierAst* id);
    KDevelop::QualifiedIdentifier identifierForNode(VariableIdentifierAst* id);


    virtual void usingDeclaration(AstNode* node, const KDevelop::DeclarationPointer& decl) {
        Q_UNUSED(node) Q_UNUSED(decl)
    }

    KDevelop::DeclarationPointer findDeclarationImport(DeclarationType declarationType, IdentifierAst* node);
    KDevelop::DeclarationPointer findDeclarationImport(DeclarationType declarationType, VariableIdentifierAst* node);
    KDevelop::DeclarationPointer findDeclarationImport(DeclarationType declarationType, AstNode* node, const KDevelop::QualifiedIdentifier& identifier);

protected:
    EditorIntegrator* m_editor;

private:
    KDevelop::DUContext* findClassContext(NamespacedIdentifierAst* className);
    KDevelop::DUContext* findClassContext(IdentifierAst* className);
    void buildNamespaceUses(NamespacedIdentifierAst* namespaces, const KDevelop::QualifiedIdentifier& identifier);

    bool m_createProblems;
    KDevelop::CursorInRevision m_offset;
    KDevelop::DUContext* m_currentContext;

    ExpressionEvaluationResult m_result;

    bool m_isAssignmentExpressionEqual;
};

}
#endif
