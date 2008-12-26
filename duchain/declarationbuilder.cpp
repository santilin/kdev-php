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

#include "declarationbuilder.h"

#include <QByteArray>

#include <ktexteditor/smartrange.h>
#include <ktexteditor/smartinterface.h>

#include <language/duchain/functiondeclaration.h>
#include <language/duchain/stringhelpers.h>

#include "phpast.h"
#include "parsesession.h"
#include "helper.h"
#include "constantdeclaration.h"
#include "classdeclaration.h"

using namespace KTextEditor;
using namespace KDevelop;

namespace Php {

DeclarationBuilder::DeclarationBuilder (ParseSession* session)
    : m_lastVariableIdentifier(0)
{
  setEditor(session);
}

DeclarationBuilder::DeclarationBuilder (EditorIntegrator* editor)
    : m_lastVariableIdentifier(0)
{
  setEditor(editor);
}

void DeclarationBuilder::closeDeclaration()
{
    if (currentDeclaration()) {
        DUChainWriteLocker lock(DUChain::lock());
        currentDeclaration()->setType(lastType());
    }

    eventuallyAssignInternalContext();

    DeclarationBuilderBase::closeDeclaration();
}

void DeclarationBuilder::visitClassDeclarationStatement(ClassDeclarationStatementAst * node)
{
    setComment(formatComment(node, editor()));

    ClassDeclaration* dec = openDefinition<ClassDeclaration>(node->className, node);
    {
        DUChainWriteLocker lock(DUChain::lock());
        dec->setKind(KDevelop::Declaration::Type);
        dec->setClassType(Php::ClassDeclarationData::Class);
    }

    DeclarationBuilderBase::visitClassDeclarationStatement(node);
    closeDeclaration();
}

void DeclarationBuilder::classContextOpened(KDevelop::DUContext* context)
{
    DUChainWriteLocker lock(DUChain::lock());
    currentDeclaration()->setInternalContext(context);
}

void DeclarationBuilder::visitInterfaceDeclarationStatement(InterfaceDeclarationStatementAst *node)
{
    setComment(formatComment(node, editor()));

    ClassDeclaration* dec = openDefinition<ClassDeclaration>(node->interfaceName, node);
    {
        DUChainWriteLocker lock(DUChain::lock());
        dec->setKind(KDevelop::Declaration::Type);
        dec->setClassType(Php::ClassDeclarationData::Interface);
    }

    DeclarationBuilderBase::visitInterfaceDeclarationStatement(node);

    closeDeclaration();
}


void DeclarationBuilder::visitClassStatement(ClassStatementAst *node)
{
    setComment(formatComment(node, editor()));
    if (node->methodName) {
        //method declaration
        openDefinition<ClassFunctionDeclaration>(node->methodName, node);
        ClassFunctionDeclaration* dec = dynamic_cast<ClassFunctionDeclaration*>(currentDeclaration());
        Q_ASSERT(dec);
        {
            DUChainWriteLocker lock(DUChain::lock());
            dec->setKind(Declaration::Type);
            if (node->modifiers->modifiers & ModifierPublic) {
                dec->setAccessPolicy(Declaration::Public);
            } else if (node->modifiers->modifiers & ModifierProtected) {
                dec->setAccessPolicy(Declaration::Protected);
            } else if (node->modifiers->modifiers & ModifierPrivate) {
                dec->setAccessPolicy(Declaration::Private);
            }
            if (node->modifiers->modifiers & ModifierStatic) {
                dec->setStatic(true);
            }
            if (node->modifiers->modifiers & ModifierFinal) {
                //TODO: store this somewhere
            }
            if (node->modifiers->modifiers & ModifierAbstract) {
                //TODO: check if class is abstract
                //TODO: check if no methodBody exists
                //TODO: check if parent is not an interface
                //TODO: store somewhere that this function is abstract (?)
            }
            //TODO: if class is interface check if no methodBody exists
        }

        DeclarationBuilderBase::visitClassStatement(node);

        closeDeclaration();
    } else {
        if (node->modifiers) {
            m_currentModifers = node->modifiers->modifiers;
        } else {
            m_currentModifers = 0;
        }
        DeclarationBuilderBase::visitClassStatement(node);
        m_currentModifers = 0;
    }
}

void DeclarationBuilder::visitClassExtends(ClassExtendsAst *node)
{
    Declaration* dec = findDeclarationImport(currentContext(), ClassDeclarationType, node->identifier);
    if (dec) {
        StructureType::Ptr extends = StructureType::Ptr::dynamicCast(dec->abstractType());
        if (extends) {
            addBaseType(extends, false);
        }
    }
}


void DeclarationBuilder::visitClassImplements(ClassImplementsAst *node)
{
    const KDevPG::ListNode<IdentifierAst*> *__it = node->implementsSequence->front(), *__end = __it;
    do {
        Declaration* dec = findDeclarationImport(currentContext(), ClassDeclarationType, __it->element);
        if (dec) {
            StructureType::Ptr interface = StructureType::Ptr::dynamicCast(dec->abstractType());
            if (!interface) {
                //TODO report error
            } else {
                Php::ClassDeclarationData::ClassType t;
                {
                    DUChainReadLocker lock(DUChain::lock());
                    ClassDeclaration* ifDec = dynamic_cast<ClassDeclaration*>(interface->declaration(currentContext()->topContext()));
                    t = ifDec->classType();
                }
                if (t == Php::ClassDeclarationData::Interface) {
                    addBaseType(interface, true);
                } else {
                    //TODO report error
                }
            }
        }
        __it = __it->next;
    }
    while (__it != __end);
    DeclarationBuilderBase::visitClassImplements(node);
}


void DeclarationBuilder::visitClassVariable(ClassVariableAst *node)
{
    {
        DUChainWriteLocker lock(DUChain::lock());
        SimpleRange newRange = editorFindRange(node->variable, node->variable);
        openDefinition<ClassMemberDeclaration>(identifierForNode(node->variable), newRange);

        ClassMemberDeclaration* dec = dynamic_cast<ClassMemberDeclaration*>(currentDeclaration());
        Q_ASSERT(dec);
        if (m_currentModifers & ModifierPublic) {
            dec->setAccessPolicy(Declaration::Public);
        } else if (m_currentModifers & ModifierProtected) {
            dec->setAccessPolicy(Declaration::Protected);
        } else if (m_currentModifers & ModifierPrivate) {
            dec->setAccessPolicy(Declaration::Private);
        }
        if (m_currentModifers & ModifierStatic) {
            dec->setStatic(true);
        }
        if (m_currentModifers & ModifierFinal) {
            //TODO report error
        }
        if (m_currentModifers & ModifierAbstract) {
            //TODO report error
        }
        dec->setKind(Declaration::Instance);
    }

    DeclarationBuilderBase::visitClassVariable(node);
    closeDeclaration();
}

void DeclarationBuilder::visitClassConstantDeclaration(ClassConstantDeclarationAst *node)
{
    openDefinition<ClassMemberDeclaration>(node->identifier, node->identifier);
    ClassMemberDeclaration* dec = dynamic_cast<ClassMemberDeclaration*>(currentDeclaration());
    Q_ASSERT(dec);
    dec->setAccessPolicy(Declaration::Public);
    dec->setStatic(true);
    dec->setKind(Declaration::Instance);
    DeclarationBuilderBase::visitClassConstantDeclaration(node);
    closeDeclaration();
}

//copied from cpp
void DeclarationBuilder::classTypeOpened(AbstractType::Ptr type)
{
    //We override this so we can get the class-declaration into a usable state(with filled type) earlier
    DUChainWriteLocker lock(DUChain::lock());

    IdentifiedType* idType = dynamic_cast<IdentifiedType*>(type.unsafeData());

    if( idType && !idType->declarationId().isValid() ) //When the given type has no declaration yet, assume we are declaring it now
        idType->setDeclaration( currentDeclaration() );

    currentDeclaration()->setType(type);
}

void DeclarationBuilder::visitParameter(ParameterAst *node)
{
    if (node->defaultValue) {
        AbstractFunctionDeclaration* funDec = dynamic_cast<AbstractFunctionDeclaration*>(currentDeclaration());
        if (funDec) {
            funDec->addDefaultParameter(IndexedString(editor()->parseSession()->symbol(node->defaultValue)));
        }
    }
    {
        DUChainWriteLocker lock(DUChain::lock());
        SimpleRange newRange = editorFindRange(node->variable, node->variable);
        openDefinition<Declaration>(identifierForNode(node->variable), newRange);
        currentDeclaration()->setKind(Declaration::Instance);
    }

    DeclarationBuilderBase::visitParameter(node);
    closeDeclaration();
}

void DeclarationBuilder::visitFunctionDeclarationStatement(FunctionDeclarationStatementAst* node)
{
    setComment(formatComment(node, editor()));

    openDefinition<FunctionDeclaration>(node->functionName, node);
    currentDeclaration()->setKind(Declaration::Type);

    DeclarationBuilderBase::visitFunctionDeclarationStatement(node);

    closeDeclaration();
}

void DeclarationBuilder::visitExpr(ExprAst *node)
{
    m_lastVariableIdentifier = 0;
    DeclarationBuilderBase::visitExpr(node);
}

void DeclarationBuilder::visitAssignmentExpressionEqual(AssignmentExpressionEqualAst *node)
{
    VariableIdentifierAst* leftSideVariableIdentifier = m_lastVariableIdentifier;
    DeclarationBuilderBase::visitAssignmentExpressionEqual(node);

    if (leftSideVariableIdentifier && currentAbstractType()) {

        //create new declaration for every assignment
        //TODO: don't create the same twice
        DUChainWriteLocker lock(DUChain::lock());
        SimpleRange newRange = editorFindRange(leftSideVariableIdentifier, leftSideVariableIdentifier);
        openDefinition<Declaration>(identifierForNode(leftSideVariableIdentifier), newRange);
        currentDeclaration()->setKind(Declaration::Instance);

        //own closeDeclaration() that uses currentAbstractType() instead of lastType()
        currentDeclaration()->setType(currentAbstractType());
        eventuallyAssignInternalContext();
        DeclarationBuilderBase::closeDeclaration();
    }
}

void DeclarationBuilder::visitCompoundVariableWithSimpleIndirectReference(CompoundVariableWithSimpleIndirectReferenceAst *node)
{
    //needed in assignmentExpressionEqual
    m_lastVariableIdentifier = node->variable;
    DeclarationBuilderBase::visitCompoundVariableWithSimpleIndirectReference(node);
}

void DeclarationBuilder::visitFunctionCall(FunctionCallAst* node)
{
    DeclarationBuilderBase::visitFunctionCall(node);
    if (node->stringFunctionNameOrClass && !node->stringFunctionName && !node->varFunctionName) {
        if (identifierForNode(node->stringFunctionNameOrClass) == QualifiedIdentifier("define")
            && node->stringParameterList->parametersSequence->count() > 0) {
            //constant, defined through define-function

            //find name of the constant (first argument of the function call)
            CommonScalarAst* scalar = findCommonScalar(node->stringParameterList->parametersSequence->at(0)->element);
            if (scalar && scalar->string != -1) {
                QString constant = editor()->parseSession()->symbol(scalar->string);
                constant = constant.mid(1, constant.length()-2);
                SimpleRange newRange = editorFindRange(scalar, scalar);
                DUChainWriteLocker lock(DUChain::lock());
                LockedSmartInterface iface = editor()->smart();
                injectContext(iface, currentContext()->topContext()); //constants are always global
                openDefinition<ConstantDeclaration>(QualifiedIdentifier(constant), newRange);
                currentDeclaration()->setKind(Declaration::Instance);
                closeDeclaration();
                closeInjectedContext(iface);
            }
        }
    }
}
void DeclarationBuilder::visitStatement(StatementAst* node)
{
    DeclarationBuilderBase::visitStatement(node);

    if (node->foreachExprAsVar) {
        DUChainWriteLocker lock(DUChain::lock());
        SimpleRange newRange = editorFindRange(node->foreachExprAsVar, node->foreachExprAsVar);
        openDefinition<Declaration>(identifierForNode(node->foreachExprAsVar), newRange);
        currentDeclaration()->setKind(Declaration::Instance);
        closeDeclaration();
    }

    if (node->foreachVarAsVar) {
        DUChainWriteLocker lock(DUChain::lock());
        SimpleRange newRange = editorFindRange(node->foreachVarAsVar->variable, node->foreachVarAsVar->variable);
        openDefinition<Declaration>(identifierForNode(node->foreachVarAsVar->variable), newRange);
        currentDeclaration()->setKind(Declaration::Instance);
        closeDeclaration();
    }

    if (node->foreachVariable) {
        DUChainWriteLocker lock(DUChain::lock());
        SimpleRange newRange = editorFindRange(node->foreachVariable->variable, node->foreachVariable->variable);
        openDefinition<Declaration>(identifierForNode(node->foreachVariable->variable), newRange);
        currentDeclaration()->setKind(Declaration::Instance);
        closeDeclaration();
    }

}

Declaration* DeclarationBuilder::findDeclarationImport(DUContext* currentContext,
                            DeclarationType declarationType, IdentifierAst* node)
{
    return findDeclarationImportHelper(currentContext, identifierForNode(node), declarationType, node, editor(), true);
}

Declaration* DeclarationBuilder::findDeclarationImport(DUContext* currentContext,
                            DeclarationType declarationType, VariableIdentifierAst* node)
{
    return findDeclarationImportHelper(currentContext, identifierForNode(node), declarationType, node, editor(), true);
}

}
