/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <ole2.h>
#include <oleidl.h>
#include <shlobj.h>
#include <shlwapi.h>

// shellapi.h is needed to build with WIN32_LEAN_AND_MEAN
#include <shellapi.h>

#include "nsDragService.h"
#include "nsITransferable.h"
#include "nsDataObj.h"

#include "nsWidgetsCID.h"
#include "nsNativeDragTarget.h"
#include "nsNativeDragSource.h"
#include "nsClipboard.h"
#include "nsISupportsArray.h"
#include "nsIDocument.h"
#include "nsDataObjCollection.h"

#include "nsAutoPtr.h"

#include "nsString.h"
#include "nsEscape.h"
#include "nsISupportsPrimitives.h"
#include "nsNetUtil.h"
#include "nsIURL.h"
#include "nsCWebBrowserPersist.h"
#include "nsToolkit.h"
#include "nsCRT.h"
#include "nsDirectoryServiceDefs.h"
#include "nsUnicharUtils.h"
#include "gfxASurface.h"
#include "gfxContext.h"
#include "nsRect.h"
#include "nsMathUtils.h"

//-------------------------------------------------------------------------
//
// DragService constructor
//
//-------------------------------------------------------------------------
nsDragService::nsDragService()
  : mDataObject(nullptr), mSentLocalDropEvent(false)
{
}

//-------------------------------------------------------------------------
//
// DragService destructor
//
//-------------------------------------------------------------------------
nsDragService::~nsDragService()
{
  NS_IF_RELEASE(mDataObject);
}

bool
nsDragService::CreateDragImage(nsIDOMNode *aDOMNode,
                               nsIScriptableRegion *aRegion,
                               SHDRAGIMAGE *psdi)
{
  if (!psdi)
    return false;

  memset(psdi, 0, sizeof(SHDRAGIMAGE));
  if (!aDOMNode) 
    return false;

  // Prepare the drag image
  nsIntRect dragRect;
  nsRefPtr<gfxASurface> surface;
  nsPresContext* pc;
  DrawDrag(aDOMNode, aRegion,
           mScreenX, mScreenY,
           &dragRect, getter_AddRefs(surface), &pc);
  if (!surface)
    return false;

  uint32_t bmWidth = dragRect.width, bmHeight = dragRect.height;

  if (bmWidth == 0 || bmHeight == 0)
    return false;

  psdi->crColorKey = CLR_NONE;

  nsRefPtr<gfxImageSurface> imgSurface = new gfxImageSurface(
    gfxIntSize(bmWidth, bmHeight), 
    gfxImageSurface::ImageFormatARGB32);
  if (!imgSurface)
    return false;

  nsRefPtr<gfxContext> context = new gfxContext(imgSurface);
  if (!context)
    return false;

  context->SetOperator(gfxContext::OPERATOR_SOURCE);
  context->SetSource(surface);
  context->Paint();

  BITMAPV5HEADER bmih;
  memset((void*)&bmih, 0, sizeof(BITMAPV5HEADER));
  bmih.bV5Size        = sizeof(BITMAPV5HEADER);
  bmih.bV5Width       = bmWidth;
  bmih.bV5Height      = -(int32_t)bmHeight; // flip vertical
  bmih.bV5Planes      = 1;
  bmih.bV5BitCount    = 32;
  bmih.bV5Compression = BI_BITFIELDS;
  bmih.bV5RedMask     = 0x00FF0000;
  bmih.bV5GreenMask   = 0x0000FF00;
  bmih.bV5BlueMask    = 0x000000FF;
  bmih.bV5AlphaMask   = 0xFF000000;

  HDC hdcSrc = CreateCompatibleDC(NULL);
  void *lpBits = NULL;
  if (hdcSrc) {
    psdi->hbmpDragImage = 
    ::CreateDIBSection(hdcSrc, (BITMAPINFO*)&bmih, DIB_RGB_COLORS,
                       (void**)&lpBits, NULL, 0);
    if (psdi->hbmpDragImage && lpBits) {
      memcpy(lpBits,imgSurface->Data(),(bmWidth*bmHeight*4));
    }

    psdi->sizeDragImage.cx = bmWidth;
    psdi->sizeDragImage.cy = bmHeight;

    // Mouse position in center
    if (mScreenX == -1 || mScreenY == -1) {
      psdi->ptOffset.x = (uint32_t)((float)bmWidth/2.0f);
      psdi->ptOffset.y = (uint32_t)((float)bmHeight/2.0f);
    } else {
      int32_t sx = mScreenX, sy = mScreenY;
      ConvertToUnscaledDevPixels(pc, &sx, &sy);
      psdi->ptOffset.x = sx - dragRect.x;
      psdi->ptOffset.y = sy - dragRect.y;
    }

    DeleteDC(hdcSrc);
  }

  return psdi->hbmpDragImage != NULL;
}

//-------------------------------------------------------------------------
NS_IMETHODIMP
nsDragService::InvokeDragSession(nsIDOMNode *aDOMNode,
                                 nsISupportsArray *anArrayTransferables,
                                 nsIScriptableRegion *aRegion,
                                 uint32_t aActionType)
{
  nsresult rv = nsBaseDragService::InvokeDragSession(aDOMNode,
                                                     anArrayTransferables,
                                                     aRegion,
                                                     aActionType);
  NS_ENSURE_SUCCESS(rv, rv);

  // Try and get source URI of the items that are being dragged
  nsIURI *uri = nullptr;

  nsCOMPtr<nsIDocument> doc(do_QueryInterface(mSourceDocument));
  if (doc) {
    uri = doc->GetDocumentURI();
  }

  uint32_t numItemsToDrag = 0;
  rv = anArrayTransferables->Count(&numItemsToDrag);
  if (!numItemsToDrag)
    return NS_ERROR_FAILURE;

  // The clipboard class contains some static utility methods that we
  // can use to create an IDataObject from the transferable

  // if we're dragging more than one item, we need to create a
  // "collection" object to fake out the OS. This collection contains
  // one |IDataObject| for each transferable. If there is just the one
  // (most cases), only pass around the native |IDataObject|.
  nsRefPtr<IDataObject> itemToDrag;
  if (numItemsToDrag > 1) {
    nsDataObjCollection * dataObjCollection = new nsDataObjCollection();
    if (!dataObjCollection)
      return NS_ERROR_OUT_OF_MEMORY;
    itemToDrag = dataObjCollection;
    for (uint32_t i=0; i<numItemsToDrag; ++i) {
      nsCOMPtr<nsISupports> supports;
      anArrayTransferables->GetElementAt(i, getter_AddRefs(supports));
      nsCOMPtr<nsITransferable> trans(do_QueryInterface(supports));
      if (trans) {
        nsRefPtr<IDataObject> dataObj;
        rv = nsClipboard::CreateNativeDataObject(trans,
                                                 getter_AddRefs(dataObj), uri);
        NS_ENSURE_SUCCESS(rv, rv);
        // Add the flavors to the collection object too
        rv = nsClipboard::SetupNativeDataObject(trans, dataObjCollection);
        NS_ENSURE_SUCCESS(rv, rv);

        dataObjCollection->AddDataObject(dataObj);
      }
    }
  } // if dragging multiple items
  else {
    nsCOMPtr<nsISupports> supports;
    anArrayTransferables->GetElementAt(0, getter_AddRefs(supports));
    nsCOMPtr<nsITransferable> trans(do_QueryInterface(supports));
    if (trans) {
      rv = nsClipboard::CreateNativeDataObject(trans,
                                               getter_AddRefs(itemToDrag),
                                               uri);
      NS_ENSURE_SUCCESS(rv, rv);
    }
  } // else dragging a single object

  // Create a drag image if support is available
  IDragSourceHelper *pdsh;
  if (SUCCEEDED(CoCreateInstance(CLSID_DragDropHelper, NULL, CLSCTX_INPROC_SERVER,
                                 IID_IDragSourceHelper, (void**)&pdsh))) {
    SHDRAGIMAGE sdi;
    if (CreateDragImage(aDOMNode, aRegion, &sdi)) {
      if (FAILED(pdsh->InitializeFromBitmap(&sdi, itemToDrag)))
        DeleteObject(sdi.hbmpDragImage);
    }
    pdsh->Release();
  }

  // Kick off the native drag session
  return StartInvokingDragSession(itemToDrag, aActionType);
}

//-------------------------------------------------------------------------
NS_IMETHODIMP
nsDragService::StartInvokingDragSession(IDataObject * aDataObj,
                                        uint32_t aActionType)
{
  // To do the drag we need to create an object that
  // implements the IDataObject interface (for OLE)
  nsRefPtr<nsNativeDragSource> nativeDragSrc =
    new nsNativeDragSource(mDataTransfer);

  // Now figure out what the native drag effect should be
  DWORD winDropRes;
  DWORD effects = DROPEFFECT_SCROLL;
  if (aActionType & DRAGDROP_ACTION_COPY) {
    effects |= DROPEFFECT_COPY;
  }
  if (aActionType & DRAGDROP_ACTION_MOVE) {
    effects |= DROPEFFECT_MOVE;
  }
  if (aActionType & DRAGDROP_ACTION_LINK) {
    effects |= DROPEFFECT_LINK;
  }

  // XXX not sure why we bother to cache this, it can change during
  // the drag
  mDragAction = aActionType;
  mSentLocalDropEvent = false;

  // Start dragging
  StartDragSession();
  OpenDragPopup();

  nsRefPtr<IAsyncOperation> pAsyncOp;
  // Offer to do an async drag
  if (SUCCEEDED(aDataObj->QueryInterface(IID_IAsyncOperation,
                                         getter_AddRefs(pAsyncOp)))) {
    pAsyncOp->SetAsyncMode(VARIANT_TRUE);
  } else {
    NS_NOTREACHED("When did our data object stop being async");
  }

  // Call the native D&D method
  HRESULT res = ::DoDragDrop(aDataObj, nativeDragSrc, effects, &winDropRes);

  // In  cases where the drop operation completed outside the application, update
  // the source node's nsIDOMDataTransfer dropEffect value so it is up to date.  
  if (!mSentLocalDropEvent) {
    uint32_t dropResult;
    // Order is important, since multiple flags can be returned.
    if (winDropRes & DROPEFFECT_COPY)
        dropResult = DRAGDROP_ACTION_COPY;
    else if (winDropRes & DROPEFFECT_LINK)
        dropResult = DRAGDROP_ACTION_LINK;
    else if (winDropRes & DROPEFFECT_MOVE)
        dropResult = DRAGDROP_ACTION_MOVE;
    else
        dropResult = DRAGDROP_ACTION_NONE;
    
    if (mDataTransfer) {
      if (res == DRAGDROP_S_DROP) // Success 
        mDataTransfer->SetDropEffectInt(dropResult);
      else
        mDataTransfer->SetDropEffectInt(DRAGDROP_ACTION_NONE);
    }
  }

  mUserCancelled = nativeDragSrc->UserCancelled();

  // We're done dragging, get the cursor position and end the drag
  // Use GetMessagePos to get the position of the mouse at the last message
  // seen by the event loop. (Bug 489729)
  DWORD pos = ::GetMessagePos();
  POINT cpos;
  cpos.x = GET_X_LPARAM(pos);
  cpos.y = GET_Y_LPARAM(pos);
  SetDragEndPoint(nsIntPoint(cpos.x, cpos.y));
  EndDragSession(true);

  mDoingDrag = false;

  return DRAGDROP_S_DROP == res ? NS_OK : NS_ERROR_FAILURE;
}

//-------------------------------------------------------------------------
// Make Sure we have the right kind of object
nsDataObjCollection*
nsDragService::GetDataObjCollection(IDataObject* aDataObj)
{
  nsDataObjCollection * dataObjCol = nullptr;
  if (aDataObj) {
    nsIDataObjCollection* dataObj;
    if (aDataObj->QueryInterface(IID_IDataObjCollection,
                                 (void**)&dataObj) == S_OK) {
      dataObjCol = static_cast<nsDataObjCollection*>(aDataObj);
      dataObj->Release();
    }
  }

  return dataObjCol;
}

//-------------------------------------------------------------------------
NS_IMETHODIMP
nsDragService::GetNumDropItems(uint32_t * aNumItems)
{
  if (!mDataObject) {
    *aNumItems = 0;
    return NS_OK;
  }

  if (IsCollectionObject(mDataObject)) {
    nsDataObjCollection * dataObjCol = GetDataObjCollection(mDataObject);
    if (dataObjCol) {
      *aNumItems = dataObjCol->GetNumDataObjects();
    }
    else {
      // If the count cannot be determined just return 0.
      // This can happen if we have collection data of type 
      // MULTI_MIME ("Mozilla/IDataObjectCollectionFormat") on the clipboard
      // from another process but we can't obtain an IID_IDataObjCollection
      // from this process.
      *aNumItems = 0;
    }
  }
  else {
    // Next check if we have a file drop. Return the number of files in
    // the file drop as the number of items we have, pretending like we
    // actually have > 1 drag item.
    FORMATETC fe2;
    SET_FORMATETC(fe2, CF_HDROP, 0, DVASPECT_CONTENT, -1, TYMED_HGLOBAL);
    if (mDataObject->QueryGetData(&fe2) == S_OK) {
      STGMEDIUM stm;
      if (mDataObject->GetData(&fe2, &stm) == S_OK) {
        HDROP hdrop = (HDROP)GlobalLock(stm.hGlobal);
        *aNumItems = ::DragQueryFileW(hdrop, 0xFFFFFFFF, NULL, 0);
        ::GlobalUnlock(stm.hGlobal);
        ::ReleaseStgMedium(&stm);
        // Data may be provided later, so assume we have 1 item
        if (*aNumItems == 0)
          *aNumItems = 1;
      }
      else
        *aNumItems = 1;
    }
    else
      *aNumItems = 1;
  }

  return NS_OK;
}

//-------------------------------------------------------------------------
NS_IMETHODIMP
nsDragService::GetData(nsITransferable * aTransferable, uint32_t anItem)
{
  // This typcially happens on a drop, the target would be asking
  // for it's transferable to be filled in
  // Use a static clipboard utility method for this
  if (!mDataObject)
    return NS_ERROR_FAILURE;

  nsresult dataFound = NS_ERROR_FAILURE;

  if (IsCollectionObject(mDataObject)) {
    // multiple items, use |anItem| as an index into our collection
    nsDataObjCollection * dataObjCol = GetDataObjCollection(mDataObject);
    uint32_t cnt = dataObjCol->GetNumDataObjects();
    if (anItem >= 0 && anItem < cnt) {
      IDataObject * dataObj = dataObjCol->GetDataObjectAt(anItem);
      dataFound = nsClipboard::GetDataFromDataObject(dataObj, 0, nullptr,
                                                     aTransferable);
    }
    else
      NS_WARNING("Index out of range!");
  }
  else {
    // If they are asking for item "0", we can just get it...
    if (anItem == 0) {
       dataFound = nsClipboard::GetDataFromDataObject(mDataObject, anItem,
                                                      nullptr, aTransferable);
    } else {
      // It better be a file drop, or else non-zero indexes are invalid!
      FORMATETC fe2;
      SET_FORMATETC(fe2, CF_HDROP, 0, DVASPECT_CONTENT, -1, TYMED_HGLOBAL);
      if (mDataObject->QueryGetData(&fe2) == S_OK)
        dataFound = nsClipboard::GetDataFromDataObject(mDataObject, anItem,
                                                       nullptr, aTransferable);
      else
        NS_WARNING("Reqesting non-zero index, but clipboard data is not a collection!");
    }
  }
  return dataFound;
}

//---------------------------------------------------------
NS_IMETHODIMP
nsDragService::SetIDataObject(IDataObject * aDataObj)
{
  // When the native drag starts the DragService gets
  // the IDataObject that is being dragged
  NS_IF_RELEASE(mDataObject);
  mDataObject = aDataObj;
  NS_IF_ADDREF(mDataObject);

  return NS_OK;
}

//---------------------------------------------------------
void
nsDragService::SetDroppedLocal()
{
  // Sent from the native drag handler, letting us know
  // a drop occurred within the application vs. outside of it.
  mSentLocalDropEvent = true;
  return;
}

//-------------------------------------------------------------------------
NS_IMETHODIMP
nsDragService::IsDataFlavorSupported(const char *aDataFlavor, bool *_retval)
{
  if (!aDataFlavor || !mDataObject || !_retval)
    return NS_ERROR_FAILURE;

#ifdef DEBUG
  if (strcmp(aDataFlavor, kTextMime) == 0)
    NS_WARNING("DO NOT USE THE text/plain DATA FLAVOR ANY MORE. USE text/unicode INSTEAD");
#endif

  *_retval = false;

  FORMATETC fe;
  UINT format = 0;

  if (IsCollectionObject(mDataObject)) {
    // We know we have one of our special collection objects.
    format = nsClipboard::GetFormat(aDataFlavor);
    SET_FORMATETC(fe, format, 0, DVASPECT_CONTENT, -1,
                  TYMED_HGLOBAL | TYMED_FILE | TYMED_GDI);

    // See if any one of the IDataObjects in the collection supports
    // this data type
    nsDataObjCollection* dataObjCol = GetDataObjCollection(mDataObject);
    if (dataObjCol) {
      uint32_t cnt = dataObjCol->GetNumDataObjects();
      for (uint32_t i=0;i<cnt;++i) {
        IDataObject * dataObj = dataObjCol->GetDataObjectAt(i);
        if (S_OK == dataObj->QueryGetData(&fe))
          *_retval = true;             // found it!
      }
    }
  } // if special collection object
  else {
    // Ok, so we have a single object. Check to see if has the correct
    // data type. Since this can come from an outside app, we also
    // need to see if we need to perform text->unicode conversion if
    // the client asked for unicode and it wasn't available.
    format = nsClipboard::GetFormat(aDataFlavor);
    SET_FORMATETC(fe, format, 0, DVASPECT_CONTENT, -1,
                  TYMED_HGLOBAL | TYMED_FILE | TYMED_GDI);
    if (mDataObject->QueryGetData(&fe) == S_OK)
      *_retval = true;                 // found it!
    else {
      // We haven't found the exact flavor the client asked for, but
      // maybe we can still find it from something else that's on the
      // clipboard
      if (strcmp(aDataFlavor, kUnicodeMime) == 0) {
        // client asked for unicode and it wasn't present, check if we
        // have CF_TEXT.  We'll handle the actual data substitution in
        // the data object.
        format = nsClipboard::GetFormat(kTextMime);
        SET_FORMATETC(fe, format, 0, DVASPECT_CONTENT, -1,
                      TYMED_HGLOBAL | TYMED_FILE | TYMED_GDI);
        if (mDataObject->QueryGetData(&fe) == S_OK)
          *_retval = true;                 // found it!
      }
      else if (strcmp(aDataFlavor, kURLMime) == 0) {
        // client asked for a url and it wasn't present, but if we
        // have a file, then we have a URL to give them (the path, or
        // the internal URL if an InternetShortcut).
        format = nsClipboard::GetFormat(kFileMime);
        SET_FORMATETC(fe, format, 0, DVASPECT_CONTENT, -1,
                      TYMED_HGLOBAL | TYMED_FILE | TYMED_GDI);
        if (mDataObject->QueryGetData(&fe) == S_OK)
          *_retval = true;                 // found it!
      }
    } // else try again
  }

  return NS_OK;
}


//
// IsCollectionObject
//
// Determine if this is a single |IDataObject| or one of our private
// collection objects. We know the difference because our collection
// object will respond to supporting the private |MULTI_MIME| format.
//
bool
nsDragService::IsCollectionObject(IDataObject* inDataObj)
{
  bool isCollection = false;

  // setup the format object to ask for the MULTI_MIME format. We only
  // need to do this once
  static UINT sFormat = 0;
  static FORMATETC sFE;
  if (!sFormat) {
    sFormat = nsClipboard::GetFormat(MULTI_MIME);
    SET_FORMATETC(sFE, sFormat, 0, DVASPECT_CONTENT, -1, TYMED_HGLOBAL);
  }

  // ask the object if it supports it. If yes, we have a collection
  // object
  if (inDataObj->QueryGetData(&sFE) == S_OK)
    isCollection = true;

  return isCollection;

} // IsCollectionObject


//
// EndDragSession
//
// Override the default to make sure that we release the data object
// when the drag ends. It seems that OLE doesn't like to let apps quit
// w/out crashing when we're still holding onto their data
//
NS_IMETHODIMP
nsDragService::EndDragSession(bool aDoneDrag)
{
  nsBaseDragService::EndDragSession(aDoneDrag);
  NS_IF_RELEASE(mDataObject);

  return NS_OK;
}
