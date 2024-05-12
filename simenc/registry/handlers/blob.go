package handlers

import (
	"net/http"

	//log "github.com/Sirupsen/logrus"
	"github.com/docker/simenc"
	"github.com/docker/simenc/context"
	"github.com/docker/simenc/registry/api/errcode"
	"github.com/docker/simenc/registry/api/v2"
	"github.com/gorilla/handlers"
	digest "github.com/opencontainers/go-digest"

)

// blobDispatcher uses the request context to build a blobHandler.
func blobDispatcher(ctx *Context, r *http.Request) http.Handler {
	dgst, err := getDigest(ctx)
	if err != nil {

		if err == errDigestNotAvailable {
			return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
				ctx.Errors = append(ctx.Errors, v2.ErrorCodeDigestInvalid.WithDetail(err))
			})
		}

		return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			ctx.Errors = append(ctx.Errors, v2.ErrorCodeDigestInvalid.WithDetail(err))
		})
	}

	blobHandler := &blobHandler{
		Context: ctx,
		Digest:  dgst,
	}
	mhandler := handlers.MethodHandler{
		"GET":  http.HandlerFunc(blobHandler.GetBlob), //Head Blob GetBlob
		"HEAD": http.HandlerFunc(blobHandler.HeadBlob),
	}

	if !ctx.readOnly {
		mhandler["DELETE"] = http.HandlerFunc(blobHandler.DeleteBlob)
	}

	return mhandler
}

// blobHandler serves http blob requests.
type blobHandler struct {
	*Context

	Digest digest.Digest
}

func (bh *blobHandler) HeadBlob(w http.ResponseWriter, r *http.Request) {
	//fmt.Printf("simenc: enter HeadBlob \n")
	blobs := bh.Repository.Blobs(bh)
	//log.Warnf("FAST2: handler blob.go GetBlob")
	desc, err := blobs.Stat(bh, bh.Digest)
	if err != nil {
		if err == distribution.ErrBlobUnknown {
			bh.Errors = append(bh.Errors, v2.ErrorCodeBlobUnknown.WithDetail(bh.Digest))
		} else {
			bh.Errors = append(bh.Errors, errcode.ErrorCodeUnknown.WithDetail(err))
		}
		return
	}

	if err := blobs.ServeHeadBlob(bh, w, r, desc.Digest); err != nil {
		context.GetLogger(bh).Debugf("unexpected error getting blob HTTP handler: %v", err)
		bh.Errors = append(bh.Errors, errcode.ErrorCodeUnknown.WithDetail(err))
		return
	}
}

// GetBlob fetches the binary data from backend storage returns it in the
// response.
//Resume stars here in GetBlob
func (bh *blobHandler) GetBlob(w http.ResponseWriter, r *http.Request) {
	//fmt.Printf("simenc: enter GetBlob \n")
	blobs := bh.Repository.Blobs(bh)

	desc, err := blobs.Stat(bh, bh.Digest)
	if err != nil {
		if err == distribution.ErrBlobUnknown {
			bh.Errors = append(bh.Errors, v2.ErrorCodeBlobUnknown.WithDetail(bh.Digest))
		} else {
			bh.Errors = append(bh.Errors, errcode.ErrorCodeUnknown.WithDetail(err))
		}
//		if "PRECONSTRUCTLAYER" == handlers.GetType(bh) || "PRECONSTRUCTSLICE" == handlers.Gettype(bh){ //type == preconstruct// skip{}
//			context.GetLogger(bh).Debugf("simenc: GetBlob: skip the errors for preconstruct layer or slice for dgst: %v", bh.Digest)
//		}else{
//			return
//		}
	}

	if err := blobs.ServeBlob(bh, w, r, desc.Digest); err != nil {
		context.GetLogger(bh).Debugf("unexpected error getting blob HTTP handler: %v", err)
		bh.Errors = append(bh.Errors, errcode.ErrorCodeUnknown.WithDetail(err))
		return
	}
}

// DeleteBlob deletes a layer blob
func (bh *blobHandler) DeleteBlob(w http.ResponseWriter, r *http.Request) {
	context.GetLogger(bh).Debug("DeleteBlob")

	blobs := bh.Repository.Blobs(bh)
	err := blobs.Delete(bh, bh.Digest)
	if err != nil {
		switch err {
		case distribution.ErrUnsupported:
			bh.Errors = append(bh.Errors, errcode.ErrorCodeUnsupported)
			return
		case distribution.ErrBlobUnknown:
			bh.Errors = append(bh.Errors, v2.ErrorCodeBlobUnknown)
			return
		default:
			bh.Errors = append(bh.Errors, err)
			context.GetLogger(bh).Errorf("Unknown error deleting blob: %s", err.Error())
			return
		}
	}

	w.Header().Set("Content-Length", "0")
	w.WriteHeader(http.StatusAccepted)
}
