/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
 * The contents of this file are subject to the Netscape Public License
 * Version 1.0 (the "License"); you may not use this file except in
 * compliance with the License.  You may obtain a copy of the License at
 * http://www.mozilla.org/NPL/
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied.  See
 * the License for the specific language governing rights and limitations
 * under the License.
 *
 * The Original Code is Mozilla Communicator client code.
 *
 * The Initial Developer of the Original Code is Netscape Communications
 * Corporation.  Portions created by Netscape are Copyright (C) 1998
 * Netscape Communications Corporation.  All Rights Reserved.
 */

#ifndef nsXULTreeElement_h__
#define nsXULTreeElement_h__

#include "nsXULElement.h"
#include "nsIDOMXULTreeElement.h"
#include "nsIRDFCompositeDataSource.h"

class nsXULTreeElement : public nsXULElement,
                         public nsIDOMXULTreeElement
{
private:
    nsIRDFCompositeDataSource* mDatabase;

public:
    nsXULTreeElement(nsIDOMXULElement* aOuter)
        : nsXULElement(aOuter),
          mDatabase(nsnull)
    {
    }

    NS_DECL_ISUPPORTS_INHERITED

    // nsIDOMNode interface
    NS_FORWARD_IDOMNODE(mOuter->);

    // nsIDOMElement interface
    NS_FORWARD_IDOMELEMENT(mOuter->);

    // nsIDOMXULElement interface
    NS_FORWARD_IDOMXULELEMENT(mOuter->);

    // nsIDOMXULTreeElement interface
    NS_DECL_IDOMXULTREEELEMENT

    virtual const nsIID& GetDOMIID() {
        return nsIDOMXULTreeElement::GetIID();
    }
};


#endif // nsXULTreeElement_h__
