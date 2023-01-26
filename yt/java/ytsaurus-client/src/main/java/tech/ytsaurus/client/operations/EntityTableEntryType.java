package tech.ytsaurus.client.operations;

import java.io.BufferedOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.UncheckedIOException;
import java.util.List;
import java.util.NoSuchElementException;

import tech.ytsaurus.client.request.Format;
import tech.ytsaurus.core.operations.CloseableIterator;
import tech.ytsaurus.core.operations.OperationContext;
import tech.ytsaurus.core.operations.Yield;
import tech.ytsaurus.skiff.deserializer.EntitySkiffDeserializer;
import tech.ytsaurus.skiff.deserializer.SkiffParser;
import tech.ytsaurus.skiff.schema.SkiffSchema;
import tech.ytsaurus.skiff.schema.WireType;
import tech.ytsaurus.skiff.serializer.EntitySkiffSerializer;
import tech.ytsaurus.ysontree.YTreeStringNode;

import static tech.ytsaurus.skiff.serializer.EntitySkiffSchemaCreator.getEntitySchema;

public class EntityTableEntryType<T> implements YTableEntryType<T> {
    private static final byte[] FIRST_TABLE_INDEX = new byte[]{0, 0};
    private final Class<T> entityClass;
    private final SkiffSchema entitySchema;
    private final boolean trackIndices;
    private final boolean isInputType;

    public EntityTableEntryType(Class<T> entityClass, boolean trackIndices, boolean isInputType) {
        this.entityClass = entityClass;
        this.entitySchema = getEntitySchema(entityClass);
        if (trackIndices) {
            this.entitySchema.getChildren().add(
                    SkiffSchema.variant8(List.of(
                                    SkiffSchema.nothing(),
                                    SkiffSchema.simpleType(WireType.INT_64)
                            ))
                            .setName("$row_index")
            );
        }
        this.trackIndices = trackIndices;
        this.isInputType = isInputType;
    }

    @Override
    public YTreeStringNode format(FormatContext context) {
        int tableCount = isInputType ?
                context.getInputTableCount().orElseThrow(IllegalArgumentException::new) :
                context.getOutputTableCount().orElseThrow(IllegalArgumentException::new);
        return Format.skiff(entitySchema, tableCount).toTree().stringNode();
    }

    @Override
    public CloseableIterator<T> iterator(InputStream input, OperationContext context) {
        context.withSettingIndices(trackIndices, trackIndices);
        var parser = new SkiffParser(input);
        return new CloseableIterator<>() {
            long rowIndex = 0;
            short tableIndex = 0;

            @Override
            public boolean hasNext() {
                return parser.hasMoreData();
            }

            @Override
            public T next() {
                tableIndex = parser.parseInt16();
                var object = EntitySkiffDeserializer.deserialize(parser, entityClass, entitySchema)
                        .orElseThrow(NoSuchElementException::new);
                if (trackIndices) {
                    rowIndex++;
                    if (parser.parseVariant8Tag() != 0) {
                        rowIndex = parser.parseInt64();
                    }
                    context.setRowIndex(rowIndex);
                    context.setTableIndex(tableIndex);
                }
                return object;
            }

            @Override
            public void close() throws Exception {
                input.close();
            }

        };
    }

    @Override
    public Yield<T> yield(OutputStream[] output) {
        var bufferedOutput = new BufferedOutputStream[output.length];
        for (int i = 0; i < output.length; i++) {
            bufferedOutput[i] = new BufferedOutputStream(output[i], 1 << 16);
        }
        return new Yield<>() {
            private final EntitySkiffSerializer<T> skiffSerializer =
                    new EntitySkiffSerializer<>(entityClass);

            @Override
            public void yield(int index, T value) {
                try {
                    bufferedOutput[index].write(FIRST_TABLE_INDEX);
                } catch (IOException e) {
                    throw new UncheckedIOException(e);
                }
                skiffSerializer.serialize(value, bufferedOutput[index]);
            }

            @Override
            public void close() throws IOException {
                for (int i = 0; i < output.length; ++i) {
                    bufferedOutput[i].flush();
                    bufferedOutput[i].close();
                }
            }
        };
    }
}
