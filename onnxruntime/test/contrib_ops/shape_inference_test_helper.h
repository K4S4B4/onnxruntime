// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "gtest/gtest.h"
#include "test/providers/provider_test_utils.h"

#ifdef __GNUC__
#include "onnxruntime_config.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wignored-qualifiers"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#ifdef HAS_DEPRECATED_DECLARATIONS
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
#else
#pragma warning(push)
#pragma warning(disable : 4018) /*'expression' : signed/unsigned mismatch */
#pragma warning(disable : 4065) /*switch statement contains 'default' but no 'case' labels*/
#pragma warning(disable : 4100)
#pragma warning(disable : 4146) /*unary minus operator applied to unsigned type, result still unsigned*/
#pragma warning(disable : 4127)
#pragma warning(disable : 4244)  /*'conversion' conversion from 'type1' to 'type2', possible loss of data*/
#pragma warning(disable : 4251)  /*'identifier' : class 'type' needs to have dll-interface to be used by clients of class 'type2'*/
#pragma warning(disable : 4267)  /*'var' : conversion from 'size_t' to 'type', possible loss of data*/
#pragma warning(disable : 4305)  /*'identifier' : truncation from 'type1' to 'type2'*/
#pragma warning(disable : 4307)  /*'operator' : integral constant overflow*/
#pragma warning(disable : 4309)  /*'conversion' : truncation of constant value*/
#pragma warning(disable : 4334)  /*'operator' : result of 32-bit shift implicitly converted to 64 bits (was 64-bit shift intended?)*/
#pragma warning(disable : 4355)  /*'this' : used in base member initializer list*/
#pragma warning(disable : 4506)  /*no definition for inline function 'function'*/
#pragma warning(disable : 4800)  /*'type' : forcing value to bool 'true' or 'false' (performance warning)*/
#pragma warning(disable : 4996)  /*The compiler encountered a deprecated declaration.*/
#pragma warning(disable : 6011)  /*Dereferencing NULL pointer*/
#pragma warning(disable : 6387)  /*'value' could be '0'*/
#pragma warning(disable : 26495) /*Variable is uninitialized.*/
#endif

#include "onnx/shape_inference/implementation.h"
#include "onnx/checker.h"

#ifdef __GNUC__
#pragma GCC diagnostic pop
#else
#pragma warning(pop)
#endif

namespace onnxruntime {
namespace test {

auto schema_registry = ONNX_NAMESPACE::OpSchemaRegistry::Instance();

const std::string MS_DOMAIN = "com.microsoft";

void CheckShapeEquality(ONNX_NAMESPACE::TensorShapeProto* shape1, ONNX_NAMESPACE::TensorShapeProto* shape2) {
  EXPECT_NE(shape1, nullptr);
  EXPECT_NE(shape2, nullptr);
  if ((shape1 != nullptr) && (shape2 != nullptr)) {
    EXPECT_EQ(shape1->dim_size(), shape2->dim_size()) << "Shapes do not have same rank";
    auto min_dims = std::min(shape1->dim_size(), shape2->dim_size());
    for (int i = 0; i < min_dims; ++i) {
      auto dim1 = shape1->dim(i);
      auto dim2 = shape2->dim(i);
      EXPECT_EQ(dim1.has_dim_value(), dim2.has_dim_value());
      if (dim1.has_dim_value()) {
        EXPECT_EQ(dim1.dim_value(), dim2.dim_value());
      }
      EXPECT_EQ(dim1.has_dim_param(), dim2.has_dim_param());
      if (dim1.has_dim_param()) {
        EXPECT_EQ(dim1.dim_param(), dim2.dim_param());
      }
    }
  }
}

inline void CreateValueInfo(
    ONNX_NAMESPACE::ValueInfoProto& value_info,
    const std::string& name,
    const ONNX_NAMESPACE::TensorProto_DataType& elem_type,
    const std::vector<int64_t> shape) {
  value_info.set_name(name);
  ONNX_NAMESPACE::TypeProto* type = value_info.mutable_type();
  ONNX_NAMESPACE::TypeProto_Tensor* tensor_type = type->mutable_tensor_type();
  tensor_type->set_elem_type(elem_type);
  ONNX_NAMESPACE::TensorShapeProto* value_info_shape = tensor_type->mutable_shape();

  for (int64_t dim_value : shape) {
    value_info_shape->add_dim()->set_dim_value(dim_value);
  }
}

inline void TestShapeInference(
    const std::string& op_type,
    const std::vector<ONNX_NAMESPACE::ValueInfoProto>& inputs,
    const std::vector<ONNX_NAMESPACE::AttributeProto>& attributes,
    ONNX_NAMESPACE::ValueInfoProto& output) {
  ONNX_NAMESPACE::ModelProto model;
  // Set opset (domain + version)
  ONNX_NAMESPACE::OperatorSetIdProto* op_set_id = model.add_opset_import();
  op_set_id->set_domain(MS_DOMAIN);
  op_set_id->set_version(1);
  model.set_ir_version(6);
  model.set_producer_name("onnx");

  // Set model graph
  ONNX_NAMESPACE::GraphProto* graph = model.mutable_graph();
  graph->set_name("test-op");
  graph->add_value_info();

  // Set add operator node to graph
  auto& node = *graph->add_node();
  node.set_op_type(op_type);
  node.set_domain(MS_DOMAIN);
  node.set_name("test_node");

  // Add node inputs and graph inputs
  for (auto const& n_ : inputs) {
    node.add_input(n_.name());
    *graph->add_input() = n_;
  }

  // Add node attributes
  for (auto const& attr : attributes) {
    node.add_attribute()->CopyFrom(attr);
  }

  node.add_output("Output");

  ONNX_NAMESPACE::checker::check_model(model);
  ONNX_NAMESPACE::shape_inference::InferShapes(model, false, schema_registry);

  auto inferredGraph = model.graph();
  int index = static_cast<int>(inputs.size());  // index for value_info of output
  auto inferred_output = inferredGraph.value_info(index);

  auto elem_type = output.mutable_type()->mutable_tensor_type()->elem_type();
  auto inferred_elem_type = inferred_output.mutable_type()->mutable_tensor_type()->elem_type();
  EXPECT_EQ(elem_type, inferred_elem_type);

  auto shape = output.mutable_type()->mutable_tensor_type()->mutable_shape();
  auto inferred_shape = inferred_output.mutable_type()->mutable_tensor_type()->mutable_shape();
  CheckShapeEquality(shape, inferred_shape);
}

}  // namespace test
}  // namespace onnxruntime