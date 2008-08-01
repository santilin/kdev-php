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

#include "typebuilder.h"

#include <ktexteditor/smartrange.h>

#include <language/duchain/identifier.h>
#include <language/duchain/duchain.h>
#include <language/duchain/duchainlock.h>
#include <language/duchain/ducontext.h>
#include <language/duchain/declaration.h>
#include "editorintegrator.h"
#include "parsesession.h"
#include "phpdebugvisitor.h"
#include "expressionparser.h"

using namespace KDevelop;
namespace Php {

ClassType::Ptr TypeBuilder::parseDocComment(AstNode* node, const QString& docCommentName)
{
    QString docComment = editor()->parseSession()->docComment(node->startToken);
    if (!docComment.isEmpty()) {
        QRegExp rx("\\* +@"+QRegExp::escape(docCommentName)+" ([^ ]*)");
        if (rx.indexIn(docComment) != -1) {
            if(openTypeFromName(QualifiedIdentifier(rx.cap(1)), node, true)) {
                closeType();
                return ClassType::Ptr::dynamicCast(lastType());
            }
        }
    }
    return ClassType::Ptr();
}

FunctionType::Ptr TypeBuilder::openFunctionType(AstNode* node)
{
    FunctionType::Ptr functionType = FunctionType::Ptr(new FunctionType());

    openType(functionType);

    m_currentFunctionType = functionType;

    ClassType::Ptr returnType = parseDocComment(node, "return");
    if (returnType) {
        functionType->setReturnType(AbstractType::Ptr::staticCast(returnType));
    }

    return functionType;
}

void TypeBuilder::visitClassDeclarationStatement( ClassDeclarationStatementAst* node )
{
    ClassType::Ptr classType = ClassType::Ptr(new ClassType());
    classType->setClassType(Class);

    openType(classType);

    classTypeOpened(currentAbstractType()); //This callback is needed, because the type of the class-declaration needs to be set early so the class can be referenced from within itself

    TypeBuilderBase::visitClassDeclarationStatement(node);

    // Prevent additional elements being added if this becomes the current type again
    classType->close();

    closeType();
}

void TypeBuilder::visitInterfaceDeclarationStatement(InterfaceDeclarationStatementAst* node)
{
    ClassType::Ptr classType = ClassType::Ptr(new ClassType());
    classType->setClassType(Interface);

    openType(classType);

    classTypeOpened(currentAbstractType()); //This callback is needed, because the type of the class-declaration needs to be set early so the class can be referenced from within itself

    TypeBuilderBase::visitInterfaceDeclarationStatement(node);

    // Prevent additional elements being added if this becomes the current type again
    classType->close();

    closeType();

}

void TypeBuilder::visitClassStatement(ClassStatementAst *node)
{
    if (node->methodName) {
        //method declaration
        openFunctionType(node);
        TypeBuilderBase::visitClassStatement(node);
        m_currentFunctionType = 0;
        closeType();
    } else {
        //member-variable
        parseDocComment(node, "var"); //sets lastType(), used openDefinition
        TypeBuilderBase::visitClassStatement(node);
        clearLastType();
    }
}

void TypeBuilder::visitClassVariable(ClassVariableAst *node)
{
    TypeBuilderBase::visitClassVariable(node);
}

void TypeBuilder::visitParameter(ParameterAst *node)
{
    TypeBuilderBase::visitParameter(node);
    if (hasCurrentType()) {
        if (FunctionType::Ptr function = currentType<FunctionType>()) {
            if (node->parameterType && openTypeFromName(node->parameterType, true)) {
                closeType();
                function->addArgument(lastType());
            }
        }
    }
}

void TypeBuilder::visitFunctionDeclarationStatement(FunctionDeclarationStatementAst* node)
{
    openFunctionType(node);
    TypeBuilderBase::visitFunctionDeclarationStatement(node);
    m_currentFunctionType = 0;
    closeType();
}

void TypeBuilder::visitExpr(ExprAst *node)
{
    m_expressionType = 0;
    node->ducontext = currentContext();
    ExpressionParser ep(false, false);
    m_expressionType = ep.evaluateType(node, editor()->parseSession()/*, currentContext()->topContext*/);
    TypeBuilderBase::visitExpr(node);
}

void TypeBuilder::visitStatement(StatementAst* node)
{
    TypeBuilderBase::visitStatement(node);
    if (node->returnExpr && m_expressionType && m_currentFunctionType
            && (!m_currentFunctionType->returnType()
                || IntegralType::Ptr::dynamicCast(m_currentFunctionType->returnType())))
    {
        m_currentFunctionType->setReturnType(m_expressionType);
    }
}


}

