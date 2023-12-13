// Code generated by protoc-gen-go. DO NOT EDIT.
// versions:
// 	protoc-gen-go v1.31.0
// 	protoc        v3.19.0
// source: yt/yt_proto/yt/core/misc/proto/error.proto

package misc

import (
	ytree "go.ytsaurus.tech/yt/go/proto/core/ytree"
	protoreflect "google.golang.org/protobuf/reflect/protoreflect"
	protoimpl "google.golang.org/protobuf/runtime/protoimpl"
	reflect "reflect"
	sync "sync"
)

const (
	// Verify that this generated code is sufficiently up-to-date.
	_ = protoimpl.EnforceVersion(20 - protoimpl.MinVersion)
	// Verify that runtime/protoimpl is sufficiently up-to-date.
	_ = protoimpl.EnforceVersion(protoimpl.MaxVersion - 20)
)

type TError struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	Code        *int32                      `protobuf:"varint,1,req,name=code" json:"code,omitempty"`
	Message     *string                     `protobuf:"bytes,2,opt,name=message" json:"message,omitempty"`
	Attributes  *ytree.TAttributeDictionary `protobuf:"bytes,3,opt,name=attributes" json:"attributes,omitempty"`
	InnerErrors []*TError                   `protobuf:"bytes,4,rep,name=inner_errors,json=innerErrors" json:"inner_errors,omitempty"`
}

func (x *TError) Reset() {
	*x = TError{}
	if protoimpl.UnsafeEnabled {
		mi := &file_yt_yt_proto_yt_core_misc_proto_error_proto_msgTypes[0]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *TError) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*TError) ProtoMessage() {}

func (x *TError) ProtoReflect() protoreflect.Message {
	mi := &file_yt_yt_proto_yt_core_misc_proto_error_proto_msgTypes[0]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use TError.ProtoReflect.Descriptor instead.
func (*TError) Descriptor() ([]byte, []int) {
	return file_yt_yt_proto_yt_core_misc_proto_error_proto_rawDescGZIP(), []int{0}
}

func (x *TError) GetCode() int32 {
	if x != nil && x.Code != nil {
		return *x.Code
	}
	return 0
}

func (x *TError) GetMessage() string {
	if x != nil && x.Message != nil {
		return *x.Message
	}
	return ""
}

func (x *TError) GetAttributes() *ytree.TAttributeDictionary {
	if x != nil {
		return x.Attributes
	}
	return nil
}

func (x *TError) GetInnerErrors() []*TError {
	if x != nil {
		return x.InnerErrors
	}
	return nil
}

var File_yt_yt_proto_yt_core_misc_proto_error_proto protoreflect.FileDescriptor

var file_yt_yt_proto_yt_core_misc_proto_error_proto_rawDesc = []byte{
	0x0a, 0x2a, 0x79, 0x74, 0x2f, 0x79, 0x74, 0x5f, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x2f, 0x79, 0x74,
	0x2f, 0x63, 0x6f, 0x72, 0x65, 0x2f, 0x6d, 0x69, 0x73, 0x63, 0x2f, 0x70, 0x72, 0x6f, 0x74, 0x6f,
	0x2f, 0x65, 0x72, 0x72, 0x6f, 0x72, 0x2e, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x12, 0x0a, 0x4e, 0x59,
	0x54, 0x2e, 0x4e, 0x50, 0x72, 0x6f, 0x74, 0x6f, 0x1a, 0x2d, 0x79, 0x74, 0x5f, 0x70, 0x72, 0x6f,
	0x74, 0x6f, 0x2f, 0x79, 0x74, 0x2f, 0x63, 0x6f, 0x72, 0x65, 0x2f, 0x79, 0x74, 0x72, 0x65, 0x65,
	0x2f, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x2f, 0x61, 0x74, 0x74, 0x72, 0x69, 0x62, 0x75, 0x74, 0x65,
	0x73, 0x2e, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x22, 0xb6, 0x01, 0x0a, 0x06, 0x54, 0x45, 0x72, 0x72,
	0x6f, 0x72, 0x12, 0x12, 0x0a, 0x04, 0x63, 0x6f, 0x64, 0x65, 0x18, 0x01, 0x20, 0x02, 0x28, 0x05,
	0x52, 0x04, 0x63, 0x6f, 0x64, 0x65, 0x12, 0x18, 0x0a, 0x07, 0x6d, 0x65, 0x73, 0x73, 0x61, 0x67,
	0x65, 0x18, 0x02, 0x20, 0x01, 0x28, 0x09, 0x52, 0x07, 0x6d, 0x65, 0x73, 0x73, 0x61, 0x67, 0x65,
	0x12, 0x47, 0x0a, 0x0a, 0x61, 0x74, 0x74, 0x72, 0x69, 0x62, 0x75, 0x74, 0x65, 0x73, 0x18, 0x03,
	0x20, 0x01, 0x28, 0x0b, 0x32, 0x27, 0x2e, 0x4e, 0x59, 0x54, 0x2e, 0x4e, 0x59, 0x54, 0x72, 0x65,
	0x65, 0x2e, 0x4e, 0x50, 0x72, 0x6f, 0x74, 0x6f, 0x2e, 0x54, 0x41, 0x74, 0x74, 0x72, 0x69, 0x62,
	0x75, 0x74, 0x65, 0x44, 0x69, 0x63, 0x74, 0x69, 0x6f, 0x6e, 0x61, 0x72, 0x79, 0x52, 0x0a, 0x61,
	0x74, 0x74, 0x72, 0x69, 0x62, 0x75, 0x74, 0x65, 0x73, 0x12, 0x35, 0x0a, 0x0c, 0x69, 0x6e, 0x6e,
	0x65, 0x72, 0x5f, 0x65, 0x72, 0x72, 0x6f, 0x72, 0x73, 0x18, 0x04, 0x20, 0x03, 0x28, 0x0b, 0x32,
	0x12, 0x2e, 0x4e, 0x59, 0x54, 0x2e, 0x4e, 0x50, 0x72, 0x6f, 0x74, 0x6f, 0x2e, 0x54, 0x45, 0x72,
	0x72, 0x6f, 0x72, 0x52, 0x0b, 0x69, 0x6e, 0x6e, 0x65, 0x72, 0x45, 0x72, 0x72, 0x6f, 0x72, 0x73,
	0x42, 0x39, 0x0a, 0x0d, 0x74, 0x65, 0x63, 0x68, 0x2e, 0x79, 0x74, 0x73, 0x61, 0x75, 0x72, 0x75,
	0x73, 0x50, 0x01, 0x5a, 0x26, 0x61, 0x2e, 0x79, 0x61, 0x6e, 0x64, 0x65, 0x78, 0x2d, 0x74, 0x65,
	0x61, 0x6d, 0x2e, 0x72, 0x75, 0x2f, 0x79, 0x74, 0x2f, 0x67, 0x6f, 0x2f, 0x70, 0x72, 0x6f, 0x74,
	0x6f, 0x2f, 0x63, 0x6f, 0x72, 0x65, 0x2f, 0x6d, 0x69, 0x73, 0x63,
}

var (
	file_yt_yt_proto_yt_core_misc_proto_error_proto_rawDescOnce sync.Once
	file_yt_yt_proto_yt_core_misc_proto_error_proto_rawDescData = file_yt_yt_proto_yt_core_misc_proto_error_proto_rawDesc
)

func file_yt_yt_proto_yt_core_misc_proto_error_proto_rawDescGZIP() []byte {
	file_yt_yt_proto_yt_core_misc_proto_error_proto_rawDescOnce.Do(func() {
		file_yt_yt_proto_yt_core_misc_proto_error_proto_rawDescData = protoimpl.X.CompressGZIP(file_yt_yt_proto_yt_core_misc_proto_error_proto_rawDescData)
	})
	return file_yt_yt_proto_yt_core_misc_proto_error_proto_rawDescData
}

var file_yt_yt_proto_yt_core_misc_proto_error_proto_msgTypes = make([]protoimpl.MessageInfo, 1)
var file_yt_yt_proto_yt_core_misc_proto_error_proto_goTypes = []interface{}{
	(*TError)(nil),                     // 0: NYT.NProto.TError
	(*ytree.TAttributeDictionary)(nil), // 1: NYT.NYTree.NProto.TAttributeDictionary
}
var file_yt_yt_proto_yt_core_misc_proto_error_proto_depIdxs = []int32{
	1, // 0: NYT.NProto.TError.attributes:type_name -> NYT.NYTree.NProto.TAttributeDictionary
	0, // 1: NYT.NProto.TError.inner_errors:type_name -> NYT.NProto.TError
	2, // [2:2] is the sub-list for method output_type
	2, // [2:2] is the sub-list for method input_type
	2, // [2:2] is the sub-list for extension type_name
	2, // [2:2] is the sub-list for extension extendee
	0, // [0:2] is the sub-list for field type_name
}

func init() { file_yt_yt_proto_yt_core_misc_proto_error_proto_init() }
func file_yt_yt_proto_yt_core_misc_proto_error_proto_init() {
	if File_yt_yt_proto_yt_core_misc_proto_error_proto != nil {
		return
	}
	if !protoimpl.UnsafeEnabled {
		file_yt_yt_proto_yt_core_misc_proto_error_proto_msgTypes[0].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*TError); i {
			case 0:
				return &v.state
			case 1:
				return &v.sizeCache
			case 2:
				return &v.unknownFields
			default:
				return nil
			}
		}
	}
	type x struct{}
	out := protoimpl.TypeBuilder{
		File: protoimpl.DescBuilder{
			GoPackagePath: reflect.TypeOf(x{}).PkgPath(),
			RawDescriptor: file_yt_yt_proto_yt_core_misc_proto_error_proto_rawDesc,
			NumEnums:      0,
			NumMessages:   1,
			NumExtensions: 0,
			NumServices:   0,
		},
		GoTypes:           file_yt_yt_proto_yt_core_misc_proto_error_proto_goTypes,
		DependencyIndexes: file_yt_yt_proto_yt_core_misc_proto_error_proto_depIdxs,
		MessageInfos:      file_yt_yt_proto_yt_core_misc_proto_error_proto_msgTypes,
	}.Build()
	File_yt_yt_proto_yt_core_misc_proto_error_proto = out.File
	file_yt_yt_proto_yt_core_misc_proto_error_proto_rawDesc = nil
	file_yt_yt_proto_yt_core_misc_proto_error_proto_goTypes = nil
	file_yt_yt_proto_yt_core_misc_proto_error_proto_depIdxs = nil
}
