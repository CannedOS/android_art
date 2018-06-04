/*
 * Copyright (C) 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ART_LIBDEXFILE_DEX_CLASS_ACCESSOR_INL_H_
#define ART_LIBDEXFILE_DEX_CLASS_ACCESSOR_INL_H_

#include "class_accessor.h"

#include "base/leb128.h"
#include "class_iterator.h"
#include "code_item_accessors-inl.h"

namespace art {

inline ClassAccessor::ClassAccessor(const ClassIteratorData& data)
    : ClassAccessor(data.dex_file_, data.dex_file_.GetClassDef(data.class_def_idx_)) {}

inline ClassAccessor::ClassAccessor(const DexFile& dex_file, const DexFile::ClassDef& class_def)
    : dex_file_(dex_file),
      descriptor_index_(class_def.class_idx_),
      ptr_pos_(dex_file.GetClassData(class_def)),
      num_static_fields_(ptr_pos_ != nullptr ? DecodeUnsignedLeb128(&ptr_pos_) : 0u),
      num_instance_fields_(ptr_pos_ != nullptr ? DecodeUnsignedLeb128(&ptr_pos_) : 0u),
      num_direct_methods_(ptr_pos_ != nullptr ? DecodeUnsignedLeb128(&ptr_pos_) : 0u),
      num_virtual_methods_(ptr_pos_ != nullptr ? DecodeUnsignedLeb128(&ptr_pos_) : 0u) {}

inline void ClassAccessor::Method::Read() {
  index_ += DecodeUnsignedLeb128(&ptr_pos_);
  access_flags_ = DecodeUnsignedLeb128(&ptr_pos_);
  code_off_ = DecodeUnsignedLeb128(&ptr_pos_);
}

inline void ClassAccessor::Field::Read() {
  index_ += DecodeUnsignedLeb128(&ptr_pos_);
  access_flags_ = DecodeUnsignedLeb128(&ptr_pos_);
}

template <typename DataType, typename Visitor>
inline void ClassAccessor::VisitMembers(size_t count,
                                        const Visitor& visitor,
                                        DataType* data) const {
  DCHECK(data != nullptr);
  for ( ; count != 0; --count) {
    data->Read();
    visitor(*data);
  }
}

template <typename StaticFieldVisitor,
          typename InstanceFieldVisitor,
          typename DirectMethodVisitor,
          typename VirtualMethodVisitor>
inline void ClassAccessor::VisitFieldsAndMethods(
    const StaticFieldVisitor& static_field_visitor,
    const InstanceFieldVisitor& instance_field_visitor,
    const DirectMethodVisitor& direct_method_visitor,
    const VirtualMethodVisitor& virtual_method_visitor) const {
  Field field(dex_file_, ptr_pos_);
  VisitMembers(num_static_fields_, static_field_visitor, &field);
  field.NextSection();
  VisitMembers(num_instance_fields_, instance_field_visitor, &field);

  Method method(dex_file_, field.ptr_pos_, /*is_static_or_direct*/ true);
  VisitMembers(num_direct_methods_, direct_method_visitor, &method);
  method.NextSection();
  VisitMembers(num_virtual_methods_, virtual_method_visitor, &method);
}

template <typename DirectMethodVisitor,
          typename VirtualMethodVisitor>
inline void ClassAccessor::VisitMethods(const DirectMethodVisitor& direct_method_visitor,
                                        const VirtualMethodVisitor& virtual_method_visitor) const {
  VisitFieldsAndMethods(VoidFunctor(),
                        VoidFunctor(),
                        direct_method_visitor,
                        virtual_method_visitor);
}

template <typename StaticFieldVisitor,
          typename InstanceFieldVisitor>
inline void ClassAccessor::VisitFields(const StaticFieldVisitor& static_field_visitor,
                                       const InstanceFieldVisitor& instance_field_visitor) const {
  VisitFieldsAndMethods(static_field_visitor,
                        instance_field_visitor,
                        VoidFunctor(),
                        VoidFunctor());
}

inline const DexFile::CodeItem* ClassAccessor::GetCodeItem(const Method& method) const {
  return dex_file_.GetCodeItem(method.GetCodeItemOffset());
}

inline CodeItemInstructionAccessor ClassAccessor::Method::GetInstructions() const {
  return CodeItemInstructionAccessor(dex_file_, dex_file_.GetCodeItem(GetCodeItemOffset()));
}

inline const char* ClassAccessor::GetDescriptor() const {
  return dex_file_.StringByTypeIdx(descriptor_index_);
}

inline const DexFile::CodeItem* ClassAccessor::Method::GetCodeItem() const {
  return dex_file_.GetCodeItem(code_off_);
}

inline IterationRange<ClassAccessor::DataIterator<ClassAccessor::Field>>
    ClassAccessor::GetFieldsInternal(size_t count) const {
  return { DataIterator<Field>(dex_file_, 0u, num_static_fields_, count, ptr_pos_),
           DataIterator<Field>(dex_file_, count, num_static_fields_, count, ptr_pos_) };
}

// Return an iteration range for the first <count> methods.
inline IterationRange<ClassAccessor::DataIterator<ClassAccessor::Method>>
    ClassAccessor::GetMethodsInternal(size_t count) const {
  // Skip over the fields.
  Field field(dex_file_, ptr_pos_);
  VisitMembers(NumFields(), VoidFunctor(), &field);
  // Return the iterator pair.
  return { DataIterator<Method>(dex_file_, 0u, num_direct_methods_, count, field.ptr_pos_),
           DataIterator<Method>(dex_file_, count, num_direct_methods_, count, field.ptr_pos_) };
}

inline IterationRange<ClassAccessor::DataIterator<ClassAccessor::Field>> ClassAccessor::GetFields()
    const {
  return GetFieldsInternal(num_static_fields_ + num_instance_fields_);
}

inline IterationRange<ClassAccessor::DataIterator<ClassAccessor::Field>>
    ClassAccessor::GetStaticFields() const {
  return GetFieldsInternal(num_static_fields_);
}


inline IterationRange<ClassAccessor::DataIterator<ClassAccessor::Field>>
    ClassAccessor::GetInstanceFields() const {
  IterationRange<ClassAccessor::DataIterator<ClassAccessor::Field>> fields = GetFields();
  // Skip the static fields.
  return { std::next(fields.begin(), NumStaticFields()), fields.end() };
}

inline IterationRange<ClassAccessor::DataIterator<ClassAccessor::Method>>
    ClassAccessor::GetMethods() const {
  return GetMethodsInternal(NumMethods());
}

inline IterationRange<ClassAccessor::DataIterator<ClassAccessor::Method>>
    ClassAccessor::GetDirectMethods() const {
  return GetMethodsInternal(NumDirectMethods());
}

inline IterationRange<ClassAccessor::DataIterator<ClassAccessor::Method>>
    ClassAccessor::GetVirtualMethods() const {
  IterationRange<DataIterator<Method>> methods = GetMethods();
  // Skip the direct fields.
  return { std::next(methods.begin(), NumDirectMethods()), methods.end() };
}

inline void ClassAccessor::Field::UnHideAccessFlags() const {
  DexFile::UnHideAccessFlags(const_cast<uint8_t*>(ptr_pos_), GetAccessFlags(), /*is_method*/ false);
}

inline void ClassAccessor::Method::UnHideAccessFlags() const {
  DexFile::UnHideAccessFlags(const_cast<uint8_t*>(ptr_pos_), GetAccessFlags(), /*is_method*/ true);
}

}  // namespace art

#endif  // ART_LIBDEXFILE_DEX_CLASS_ACCESSOR_INL_H_
