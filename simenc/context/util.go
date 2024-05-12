package context

import (

	"strings"
	"time"
)

// Since looks up key, which should be a time.Time, and returns the duration
// since that time. If the key is not found, the value returned will be zero.
// This is helpful when inferring metrics related to context execution times.
func Since(ctx Context, key interface{}) time.Duration {
	if startedAt, ok := ctx.Value(key).(time.Time); ok {
		return time.Since(startedAt)
	}
	return 0
}

// GetStringValue returns a string value from the context. The empty string
// will be returned if not found.
func GetStringValue(ctx Context, key interface{}) (value string) {
	if valuev, ok := ctx.Value(key).(string); ok {
		value = valuev
	}
	return value
}

func getName(ctx Context) (name string) {
	return GetStringValue(ctx, "vars.name")
}

//TYPE XXX USRADDR XXX REPONAME XXX lowercases
//manifest or layer?
func GetType(ctx Context) (name string) {
	// varsname := GetStringValue(ctx, "http.request.remoteaddr")
	// check_uuid := GetStringValue(ctx, "http.request.method")
	// //forward_repo/forward_repo
	// if ok := strings.Contains(varsname, "forward_repo"); ok{
	// 	fmt.Println("simenc: this is forwarding! ")
	// 	return strings.ToUpper("forward_repo")
	// }
    // fmt.Println("simenc: method input: ", varsname)
	// fmt.Println("simenc: http.request.method input: ", check_uuid)
	// tmps := strings.Split(varsname, "usraddr")[0]
	// if len(tmps) < 2 {
	// 	fmt.Println("simenc: wrong input: ", tmps)
	// 	return ""
	// }
	// rtype := strings.Split(tmps, "type")[0]
	// return strings.ToUpper(rtype)
	http_request_method := GetStringValue(ctx, "http.request.method")
	rtype := strings.TrimSpace(http_request_method)
	return strings.ToUpper(rtype)
}

//usraddr
func GetUsrAddr(ctx Context) (name string) {
	// varsname := GetStringValue(ctx, "vars.name")
	
	// if ok := strings.Contains(varsname, "forward_repo"); ok{
	// 	fmt.Println("simenc: this is forwarding! ")
	// 	return strings.ToUpper("forward_repo")
	// }
	
	// tmps := strings.Split(varsname, "reponame")[0]
	// if len(tmps) < 2 {
	// 	fmt.Println("simenc: wrong input: ", tmps)
	// 	return ""
	// }
	http_request_remoteaddr := GetStringValue(ctx, "http.request.remoteaddr")
	usraddr := strings.TrimSpace(http_request_remoteaddr)
	return usraddr
}

//reponame
func GetRepoName(ctx Context) (name string) {
	varsname := GetStringValue(ctx, "vars.name")
	// fmt.Println("simenc: method input: ", varsname)
	// if ok := strings.Contains(varsname, "forward_repo"); ok{
	// 	fmt.Println("simenc: this is forwarding! ")
	// 	return strings.ToUpper("forward_repo")
	// }
	// tmps := strings.Split(varsname, "reponame")
	// if len(tmps) < 2 {
	// 	fmt.Println("simenc: wrong input: ", varsname)
	// 	return ""
	// }
	// reponame := strings.Split(varsname, "reponame")[1]
	reponame := strings.TrimSpace(varsname)
	return reponame
}
